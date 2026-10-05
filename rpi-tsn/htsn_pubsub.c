/* htsn_pubsub — implementation of the OPC UA FX field-level PubSub plane.
 *
 * One Publisher (a WriterGroup + PublishedDataSet of nodes) and one Subscriber
 * (a ReaderGroup + DataSetReader that writes into existing nodes) are shared
 * between the Controller and Device roles. The datasets reference the existing
 * address-space nodes, so the GUI / poller / CLI keep working unchanged: the
 * PubSub plane is just the wire to the peer endpoint.
 *
 * Wire contract: field order == the order in CMD_FIELDS / TELEM_FIELDS below.
 */
#include "htsn_pubsub.h"
#include "htsn_opcua_ids.h"
#include <open62541/server_pubsub.h>
#include <open62541/types_generated.h>
#include <open62541/nodeids.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A published/subscribed field: which node, its wire name, its UA_TYPES index,
 * and whether it is a 1-D dynamic array (sonar sweep). */
typedef struct {
    UA_UInt32 nodeId;   /* numeric id in namespace HTSN_OPCUA_NS */
    const char *name;   /* wire field name (contract)            */
    size_t typeIdx;     /* UA_TYPES index                         */
    int array;          /* 1 => valueRank 1 (dynamic 1-D array)   */
} ps_field_t;

/* C2D command/control dataset — RPi publishes, STM subscribes. */
static const ps_field_t CMD_FIELDS[HTSN_PUBSUB_CMD_FIELDS] = {
    { HTSN_ID_CMD_SERVO_ANGLE, "cmd_servo_angle",  UA_TYPES_INT16,   0 },
    { HTSN_ID_CMD_RELAY_ON,    "cmd_relay_on",     UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_CMD_BEEP_MS,     "cmd_beep_ms",      UA_TYPES_INT16,   0 },
    { HTSN_ID_CMD_SONAR_TRIG,  "cmd_sonar_trig",   UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_CMD_REBOOT,      "cmd_reboot",       UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_CMD_TSN_CONFIG,  "cmd_tsn_config",   UA_TYPES_STRING,  0 },
};

/* D2C telemetry dataset — STM publishes, RPi subscribes. */
static const ps_field_t TELEM_FIELDS[HTSN_PUBSUB_TELEM_FIELDS] = {
    { HTSN_ID_SONAR_SWEEP,      "sonar_sweep",      UA_TYPES_INT16,   1 },
    { HTSN_ID_SONAR_SWEEP_ID,   "sonar_sweep_id",   UA_TYPES_INT32,   0 },
    { HTSN_ID_SERVO_ANGLE,      "servo_angle",      UA_TYPES_INT16,   0 },
    { HTSN_ID_RELAY_ON,         "relay_on",         UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_BUZZER_HZ,        "buzzer_hz",        UA_TYPES_UINT16,  0 },
    { HTSN_ID_BUZZER_MS,        "buzzer_ms",        UA_TYPES_UINT16,  0 },
    { HTSN_ID_BTN1,             "btn1",             UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_BTN2,             "btn2",             UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_BTN3,             "btn3",             UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_BTN4,             "btn4",             UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_PTP_OFFSET_NS,    "ptp_offset_ns",    UA_TYPES_INT64,   0 },
    { HTSN_ID_PTP_STATE,        "ptp_state",        UA_TYPES_BYTE,    0 },
    { HTSN_ID_PTP_LOCKED,       "ptp_locked",       UA_TYPES_BOOLEAN, 0 },
    { HTSN_ID_LAST_SEEN,        "last_seen",        UA_TYPES_INT64,   0 },
    { HTSN_ID_TSN_APP_VLAN,     "tsn_app_vlan",     UA_TYPES_INT16,   0 },
    { HTSN_ID_TSN_APP_PRIO,     "tsn_app_prio",     UA_TYPES_BYTE,    0 },
    { HTSN_ID_TSN_APP_PREEMPT,  "tsn_app_preempt",  UA_TYPES_BYTE,    0 },
    { HTSN_ID_TSN_APP_TIMESYNC, "tsn_app_timesync", UA_TYPES_BYTE,    0 },
    { HTSN_ID_TSN_APP_STROLE,   "tsn_app_strole",   UA_TYPES_BYTE,    0 },
    { HTSN_ID_TSN_APP_STVLAN,   "tsn_app_stvlan",   UA_TYPES_INT16,   0 },
    { HTSN_ID_TSN_APP_TASCYC,   "tsn_app_tascyc",   UA_TYPES_INT64,   0 },
    { HTSN_ID_TSN_FEATURES,     "tsn_features",     UA_TYPES_INT32,   0 },
};

/* Build + add a Publisher: connection -> PDS -> (one DataSetField per node)
 * -> WriterGroup -> DataSetWriter. The peer subscribes to (pubid, wgid, dswid). */
static UA_StatusCode add_publisher(UA_Server *s, UA_UInt16 ns,
                                   const ps_field_t *fields, size_t nfields,
                                   const char *dest_url, const char *iface,
                                   UA_UInt16 pubid, UA_UInt16 wgid, UA_UInt16 dswid,
                                   const char *name) {
    UA_PubSubConnectionConfig cc; memset(&cc, 0, sizeof(cc));
    cc.name = UA_STRING((char *)name);
    cc.transportProfileUri = UA_STRING(HTSN_PUBSUB_TRANSPORT_UDP);
    UA_NetworkAddressUrlDataType naur = {
        UA_STRING((char *)(iface && *iface ? iface : "")),
        UA_STRING((char *)dest_url) };
    UA_Variant_setScalar(&cc.address, &naur,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    cc.publisherId.idType = UA_PUBLISHERIDTYPE_UINT16;
    cc.publisherId.id.uint16 = pubid;

    UA_NodeId connId, pdsId, wgId, dswId;
    UA_StatusCode rv = UA_Server_addPubSubConnection(s, &cc, &connId);
    if (rv != UA_STATUSCODE_GOOD) return rv;

    UA_PublishedDataSetConfig pdc; memset(&pdc, 0, sizeof(pdc));
    pdc.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
    pdc.name = UA_STRING((char *)name);
    UA_AddPublishedDataSetResult pdr = UA_Server_addPublishedDataSet(s, &pdc, &pdsId);
    if (pdr.addResult != UA_STATUSCODE_GOOD) return pdr.addResult;

    for (size_t i = 0; i < nfields; i++) {
        UA_DataSetFieldConfig fc; memset(&fc, 0, sizeof(fc));
        fc.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;
        fc.field.variable.fieldNameAlias = UA_STRING((char *)fields[i].name);
        fc.field.variable.publishParameters.publishedVariable =
            UA_NODEID_NUMERIC(ns, fields[i].nodeId);
        fc.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;
        UA_NodeId dsfId;
        UA_DataSetFieldResult fdr = UA_Server_addDataSetField(s, pdsId, &fc, &dsfId);
        if (fdr.result != UA_STATUSCODE_GOOD) return fdr.result;
    }

    UA_WriterGroupConfig wgc; memset(&wgc, 0, sizeof(wgc));
    wgc.name = UA_STRING((char *)name);
    wgc.publishingInterval = 100;   /* ms */
    wgc.writerGroupId = wgid;
    wgc.encodingMimeType = UA_PUBSUB_ENCODING_UADP;
    UA_UadpWriterGroupMessageDataType wm;
    UA_UadpWriterGroupMessageDataType_init(&wm);
    wm.networkMessageContentMask = (UA_UadpNetworkMessageContentMask)
        (UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
         UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
         UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
         UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER);
    UA_ExtensionObject_setValue(&wgc.messageSettings, &wm,
                                &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE]);
    rv = UA_Server_addWriterGroup(s, connId, &wgc, &wgId);
    if (rv != UA_STATUSCODE_GOOD) return rv;

    UA_DataSetWriterConfig dwc; memset(&dwc, 0, sizeof(dwc));
    dwc.name = UA_STRING((char *)name);
    dwc.dataSetWriterId = dswid;
    dwc.keyFrameCount = 1;   /* every frame carries all fields (small dataset) */
    return UA_Server_addDataSetWriter(s, wgId, pdsId, &dwc, &dswId);
}

/* Build + add a Subscriber: connection (bind local port) -> ReaderGroup ->
 * DataSetReader (filtered on the peer's pubid/wgid/dswid, with the DataSet
 * metadata so UADP raw fields decode) -> TargetVariables mapping each received
 * field, by index, into the existing node for that field. */
static UA_StatusCode add_subscriber(UA_Server *s, UA_UInt16 ns,
                                    const ps_field_t *fields, size_t nfields,
                                    int local_port, const char *iface,
                                    UA_UInt16 peer_pubid, UA_UInt16 peer_wgid,
                                    UA_UInt16 peer_dswid, const char *name) {
    UA_PubSubConnectionConfig cc; memset(&cc, 0, sizeof(cc));
    cc.name = UA_STRING((char *)name);
    cc.transportProfileUri = UA_STRING(HTSN_PUBSUB_TRANSPORT_UDP);
    char addr[64];
    snprintf(addr, sizeof(addr), "opc.udp://0.0.0.0:%d/", local_port);
    UA_NetworkAddressUrlDataType naur = {
        UA_STRING((char *)(iface && *iface ? iface : "")),
        UA_STRING(addr) };
    UA_Variant_setScalar(&cc.address, &naur,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    cc.publisherId.idType = UA_PUBLISHERIDTYPE_UINT32;
    cc.publisherId.id.uint32 = UA_UInt32_random();

    UA_NodeId connId, rgId, dsrId;
    UA_StatusCode rv = UA_Server_addPubSubConnection(s, &cc, &connId);
    if (rv != UA_STATUSCODE_GOOD) return rv;

    UA_ReaderGroupConfig rg; memset(&rg, 0, sizeof(rg));
    rg.name = UA_STRING((char *)name);
    rv = UA_Server_addReaderGroup(s, connId, &rg, &rgId);
    if (rv != UA_STATUSCODE_GOOD) return rv;

    UA_DataSetReaderConfig rc; memset(&rc, 0, sizeof(rc));
    rc.name = UA_STRING((char *)name);
    rc.publisherId.idType = UA_PUBLISHERIDTYPE_UINT16;
    rc.publisherId.id.uint16 = peer_pubid;
    rc.writerGroupId = peer_wgid;
    rc.dataSetWriterId = peer_dswid;

    /* DataSetMetaData drives UADP raw decoding: field name + type + rank, in
     * the same order the peer publishes. */
    UA_DataSetMetaDataType *md = &rc.dataSetMetaData;
    UA_DataSetMetaDataType_init(md);
    md->name = UA_STRING((char *)name);
    md->fieldsSize = nfields;
    md->fields = (UA_FieldMetaData *)UA_Array_new(nfields, &UA_TYPES[UA_TYPES_FIELDMETADATA]);
    for (size_t i = 0; i < nfields; i++) {
        UA_FieldMetaData_init(&md->fields[i]);
        UA_NodeId_copy(&UA_TYPES[fields[i].typeIdx].typeId, &md->fields[i].dataType);
        md->fields[i].builtInType =
            (UA_Byte)UA_TYPES[fields[i].typeIdx].typeId.identifier.numeric;
        md->fields[i].name = UA_STRING((char *)fields[i].name);
        md->fields[i].valueRank = fields[i].array ? 1 : -1;
    }

    rv = UA_Server_addDataSetReader(s, rgId, &rc, &dsrId);
    if (rv != UA_STATUSCODE_GOOD) return rv;

    UA_FieldTargetDataType *tv =
        (UA_FieldTargetDataType *)UA_calloc(nfields, sizeof(UA_FieldTargetDataType));
    for (size_t i = 0; i < nfields; i++) {
        tv[i].attributeId = UA_ATTRIBUTEID_VALUE;
        tv[i].targetNodeId = UA_NODEID_NUMERIC(ns, fields[i].nodeId);
    }
    rv = UA_Server_setDataSetReaderTargetVariables(s, dsrId, nfields, tv);
    UA_free(tv);
    return rv;
}

UA_StatusCode htsn_ps_attach_controller(UA_Server *server, UA_UInt16 ns,
                                        const char *pub_addr, int sub_port,
                                        const char *iface) {
    UA_StatusCode rv = add_publisher(server, ns, CMD_FIELDS, HTSN_PUBSUB_CMD_FIELDS,
                                     pub_addr, iface,
                                     HTSN_PUBSUB_RPI_PUBID, HTSN_PUBSUB_RPI_WGID,
                                     HTSN_PUBSUB_RPI_DSWID, "htsn_cmd");
    if (rv != UA_STATUSCODE_GOOD) return rv;
    return add_subscriber(server, ns, TELEM_FIELDS, HTSN_PUBSUB_TELEM_FIELDS,
                          sub_port, iface,
                          HTSN_PUBSUB_STM_PUBID, HTSN_PUBSUB_STM_WGID,
                          HTSN_PUBSUB_STM_DSWID, "htsn_telem");
}

UA_StatusCode htsn_ps_attach_device(UA_Server *server, UA_UInt16 ns,
                                    const char *pub_addr, int sub_port,
                                    const char *iface) {
    UA_StatusCode rv = add_publisher(server, ns, TELEM_FIELDS, HTSN_PUBSUB_TELEM_FIELDS,
                                     pub_addr, iface,
                                     HTSN_PUBSUB_STM_PUBID, HTSN_PUBSUB_STM_WGID,
                                     HTSN_PUBSUB_STM_DSWID, "htsn_telem");
    if (rv != UA_STATUSCODE_GOOD) return rv;
    return add_subscriber(server, ns, CMD_FIELDS, HTSN_PUBSUB_CMD_FIELDS,
                          sub_port, iface,
                          HTSN_PUBSUB_RPI_PUBID, HTSN_PUBSUB_RPI_WGID,
                          HTSN_PUBSUB_RPI_DSWID, "htsn_cmd");
}
