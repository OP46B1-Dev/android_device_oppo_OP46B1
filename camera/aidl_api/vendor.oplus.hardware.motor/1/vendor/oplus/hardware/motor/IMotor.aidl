///////////////////////////////////////////////////////////////////////////////
// THIS FILE IS IMMUTABLE. DO NOT EDIT IN ANY CASE.                          //
///////////////////////////////////////////////////////////////////////////////

// This file is a snapshot of an AIDL file. Do not edit it manually.
package vendor.oplus.hardware.motor;
@VintfStability
interface IMotor {
  const int DIRECTION_DOWN = 0;
  const int DIRECTION_UP = 1;
  const int START_NORMAL = 1;
  const int START_FORCE = 2;
  const int POSITION_UNKNOWN = -1;
  const int POSITION_UP = 0;
  const int POSITION_DOWN = 1;
  const int POSITION_MID = 2;
  void initializeCalibration();
  void move(int direction, int startMode);
  int getPosition();
  int getMoveState();
}
