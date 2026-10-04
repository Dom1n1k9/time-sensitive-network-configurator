/* htsn_tsn OPC UA — stable NodeId identifiers (namespace index 1 = urn:htsn:stm32).
 *
 * Shared by the RPi server (cnc_opcua.c), the STM32 client, and the GUI. The
 * numeric id (and the ns index) are the contract; keep them in sync.
 *
 *   - telemetry : written by the endpoint (STM32 client)
 *   - commands  : monitored by the endpoint (the GUI / operator writes them)
 */
#ifndef HTSN_OPCUA_IDS_H
#define HTSN_OPCUA_IDS_H

#define HTSN_OPCUA_NS       1     /* urn:htsn:stm32 */

#define HTSN_ID_OBJ         100   /* Object: stm32-tsn-01 */

/* ---- telemetry (the endpoint writes these) ---- */
#define HTSN_ID_SONAR_SWEEP      1   /* Int16[]  */
#define HTSN_ID_SONAR_SWEEP_ID   2   /* Int32    */
#define HTSN_ID_SERVO_ANGLE      3   /* Int16    */
#define HTSN_ID_RELAY_ON         4   /* Boolean  */
#define HTSN_ID_BUZZER_HZ        5   /* UInt16   */
#define HTSN_ID_BUZZER_MS        6   /* UInt16   */
#define HTSN_ID_BTN1             7   /* Boolean  */
#define HTSN_ID_BTN2             8   /* Boolean  */
#define HTSN_ID_BTN3             9   /* Boolean  */
#define HTSN_ID_BTN4             10  /* Boolean  */
#define HTSN_ID_PTP_OFFSET_NS    11  /* Int64    */
#define HTSN_ID_PTP_STATE        12  /* Byte     */
#define HTSN_ID_PTP_LOCKED       13  /* Boolean  */
#define HTSN_ID_LAST_SEEN        14  /* Int64    */

/* ---- commands (the endpoint monitors these) ---- */
#define HTSN_ID_CMD_SERVO_ANGLE   21  /* Int16    */
#define HTSN_ID_CMD_RELAY_ON      22  /* Boolean  */
#define HTSN_ID_CMD_BEEP_MS       23  /* Int16    */
#define HTSN_ID_CMD_SONAR_TRIG    24  /* Boolean  */
#define HTSN_ID_CMD_REBOOT        25  /* Boolean  */

#endif /* HTSN_OPCUA_IDS_H */
