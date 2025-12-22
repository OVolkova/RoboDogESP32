# RoboDogESP32

Firmware for Bittle X quadruped robot running on ESP32 microcontroller with autonomous balancing, complex behaviors, and multiple communication interfaces.

## Architecture Overview

### System Structure

The firmware uses a **cooperative multitasking** architecture with:
- **Main Loop (Core 1)**: Sequential polling for sensors, commands, and motion execution
- **IMU Task (Core 0)**: Dedicated FreeRTOS task for continuous gyroscope/accelerometer reading at ~200Hz

### Main Program Flow

**Setup Phase** ([RoboDog32.ino](RoboDog32.ino)):
1. Initialize I2C bus and serial communication (115200 baud)
2. Load configuration from EEPROM/Flash (ESP32 Preferences)
3. Initialize IMU (supports MPU6050 and ICM42670)
4. Setup servo system (12 PWM channels)
5. Load skill library from Flash memory
6. Initialize communication modules (Bluetooth, IR, Web Server)
7. Configure optional modules (voice, PIR sensor)
8. Set initial posture and calibration

**Main Loop** (runs continuously):
```
1. readEnvironment()     → Read sensors (IMU, sound, GPS)
2. dealWithExceptions()  → Handle IMU events (fall, lift, push)
3. Task Queue Processing → Execute queued commands OR read new signals
4. reaction()            → Process commands and generate behaviors
5. WebServerLoop()       → Handle async web requests (if enabled)
```

### Core Subsystems

#### Motion Control System ([src/motion.h](src/motion.h))
- **Servo Control**: Calibrated PWM output with angle transformation
- **Smooth Interpolation**: Frame-by-frame transformation between poses
- **IMU-Based Balancing**: Real-time gyro feedback for balance adjustment
- **Central Pattern Generator (CPG)**: Oscillator-based gait generation for walking
- **Teach Mode**: Skill learning by manually dragging joints

#### Skill Management System ([src/skill.h](src/skill.h))
Executes predefined behaviors loaded from Flash memory:
- **Postures** (period = 1): Static poses like "sit", "rest"
- **Gaits** (period > 1): Cyclic motions like "walk", "trot"
- **Behaviors** (period < 0): Complex sequences like "pushup", "pee"

Skills are stored as frame-based angle arrays with metadata (name, period, frame count).

#### IMU/Gyroscope System ([src/imu.h](src/imu.h))
- **Orientation Tracking**: Yaw/pitch/roll angles and world-frame acceleration
- **Exception Detection**: Automatically detects flipped, lifted, knocked, pushed states
- **Balance Feedback**: Provides real-time correction data for motion control
- **Dual IMU Support**: MPU6050 and ICM42670 compatibility
- **Dedicated Task**: Runs on FreeRTOS Core 0 at 5ms intervals

#### Communication System
**Input Priority** (highest to lowest):
1. Bluetooth Serial (BT_SSP)
2. Serial2 (Grove/Voice module)
3. USB Serial
4. BLE
5. Web Server

**Bluetooth Manager** ([src/bluetoothManager.h](src/bluetoothManager.h)):
- BLE Server mode for Petoi mobile app
- BLE Client mode for BBC Micro:bit
- Classic Bluetooth SSP for legacy connections
- Intelligent mode switching with timeout

**Web Server** ([src/webServer.h](src/webServer.h)):
- WiFi connection management
- Asynchronous HTTP command processing
- Browser-based robot control interface

#### Command Processing System ([src/reaction.h](src/reaction.h))
Token-based command interpreter with 30+ command types:
- `k` - Execute named skill
- `i` - Move joints simultaneously
- `c` - Calibration mode
- `d` - Rest and shutdown servos
- `g` - Toggle gyro balancing
- `b` - Play sounds

**Command Flow**:
```
Input Source → readSignal() → Parse token → reaction() → Execute → Echo confirmation
```

#### Task Queue System ([src/taskQueue.h](src/taskQueue.h))
Enables delayed and sequenced command execution:
```
qk sit:1000>m 8 0:500>
// Queue: sit skill for 1000ms, then move joint 8 to 0° after 500ms delay
```

#### Module Manager ([src/moduleManager.h](src/moduleManager.h))
Coordinates optional hardware modules:
- Voice recognition (Grove UART)
- PIR motion sensor
- Infrared remote control
- Grove serial devices

### Exception Handling

The IMU task continuously monitors for environmental changes:
- **Flipped**: Robot upside down → execute recovery skill
- **Lifted**: Robot picked up → play lifted/dropped behavior
- **Pushed**: Horizontal force → compensatory walking
- **Knocked**: Vertical shock → reaction skill
- **Turning**: Target yaw angle reached → stop rotation

### Key Design Patterns

- **Priority-based Input**: BT Serial > Serial2 > USB > BLE > Web
- **Non-blocking Execution**: No long delays in main loop, async operations
- **State Flags**: Boolean "Q" variables control features (`gyroBalanceQ`, `updateGyroQ`)
- **Event-Driven**: Polling-based sensor reading with exception-triggered reactions

### File Structure

| Component | File |
|-----------|------|
| Main entry point | [RoboDog32.ino](RoboDog32.ino) |
| Configuration | [src/RoboDog.h](src/RoboDog.h), [src/configConstants.h](src/configConstants.h) |
| Motion control | [src/motion.h](src/motion.h) |
| IMU system | [src/imu.h](src/imu.h) |
| Skill management | [src/skill.h](src/skill.h) |
| Command processor | [src/reaction.h](src/reaction.h) |
| Task queue | [src/taskQueue.h](src/taskQueue.h) |
| I/O & communication | [src/io.h](src/io.h) |
| Bluetooth | [src/bluetoothManager.h](src/bluetoothManager.h) |
| Module coordinator | [src/moduleManager.h](src/moduleManager.h) |
| Web server | [src/webServer.h](src/webServer.h) |

## Configuration Options

The firmware includes several configuration options defined in [src/RoboDog.h](src/RoboDog.h#L68-L88). You can comment/uncomment these defines to customize the firmware features.

### Feature Flags (Communication & Connectivity)

#### BT_BLE (Bluetooth Low Energy)
- Enables BLE (Bluetooth Low Energy) mode
- Used for low-power Bluetooth connections
- Commonly used for mobile app connections and wireless communication
- Defined at [src/RoboDog.h:68](src/RoboDog.h#L68)
- **Default: Enabled**

#### BT_SSP (Bluetooth Secure Simple Pairing)
- Enables classic Bluetooth using the SSP (Secure Simple Pairing) protocol
- Used for traditional Bluetooth connections with pairing/security
- Provides secure authentication and encryption
- Defined at [src/RoboDog.h:69](src/RoboDog.h#L69)
- **Default: Enabled**

#### BT_CLIENT (Bluetooth Client for Micro:Bit)
- Enables the robot to act as a BLE client (connects to other BLE devices)
- Specifically designed for connecting to BBC Micro:Bit devices
- Only defined when IR_PIN is NOT defined (hardware conflict or resource sharing)
- Defined at [src/RoboDog.h:88](src/RoboDog.h#L88)
- **Default: Disabled** (IR_PIN is defined on line 86)

#### WEB_SERVER
- Enables the built-in web server functionality
- Allows controlling the robot via WiFi through a web interface
- Provides HTTP endpoints for configuration and control
- Defined at [src/RoboDog.h:71](src/RoboDog.h#L71)
- **Default: Enabled**

#### SHOW_FPS
- Toggle FPS (Frames Per Second) display
- Useful for debugging performance
- Defined at [src/RoboDog.h:72](src/RoboDog.h#L72)
- **Default: Disabled**

### Hardware Configuration

#### BIRTHMARK
- Token character '@' used for EEPROM reset
- Send '!' token to reset the birthmark in EEPROM, triggering a restart and reset
- Defined at [src/RoboDog.h:67](src/RoboDog.h#L67)
- **Value: '@'**

#### SERVO_FREQ
- Servo motor PWM frequency
- Defined at [src/RoboDog.h:74](src/RoboDog.h#L74)
- **Value: 240 Hz**

#### MODEL
- Robot model identifier
- Defined at [src/RoboDog.h:78](src/RoboDog.h#L78)
- **Value: "Bittle X"**

#### PWM_NUM
- Number of PWM channels for servo control
- Defined at [src/RoboDog.h:83](src/RoboDog.h#L83)
- **Value: 12**

### Pin Assignments

| Pin Name | GPIO Pin | Description |
|----------|----------|-------------|
| INTERRUPT_PIN | 26 | IMU interrupt pin |
| BUZZER | 25 | Buzzer output |
| IR_PIN | 23 | Infrared receiver |
| ANALOG1 | 34 | Analog input 1 |
| ANALOG2 | 35 | Analog input 2 |
| ANALOG3 | 36 | Analog input 3 |
| ANALOG4 | 39 | Analog input 4 |

## Command Reference

The robot uses a token-based command protocol defined in [src/RoboDog.h](src/RoboDog.h#L153-L229). Commands consist of a single character token followed by optional parameters.

### Motion & Skill Commands

| Token | Name | Description | Example |
|-------|------|-------------|---------|
| `k` | T_SKILL | Execute named skill from Flash memory | `k sit` - execute "sit" skill<br>`k walk` - start walking gait |
| `K` | T_SKILL_DATA | Upload custom skill data via serial | Used for uploading new skills |
| `i` | T_INDEXED_SIMULTANEOUS_ASC | Move multiple joints simultaneously (ASCII format) | `i 0 70 8 -20 9 -20` - move joints 0, 8, 9<br>`i` alone frees head joints |
| `I` | T_INDEXED_SIMULTANEOUS_BIN | Move multiple joints simultaneously (Binary format) | `I 0 70 8 -20 9 -20` |
| `m` | T_INDEXED_SEQUENTIAL_ASC | Move joints sequentially (ASCII format) | `m 0 70 0 -70 8 -20 9 -20` |
| `M` | T_INDEXED_SEQUENTIAL_BIN | Move joints sequentially (Binary format) | `M 0 70 0 -70 8 -20 9 -20` |
| `L` | T_LISTED_BIN | Set all joint angles as list | `L angle0 angle1 ... angle15` |
| `d` | T_REST | Rest posture and shut down servos | `d` - all servos off<br>`d 8` - turn off servo 8 |
| `p` | T_PAUSE | Pause execution | `p` |
| `r` | T_CPG | Central Pattern Generator (ASCII) | Generate oscillating gait patterns |
| `Q` | T_CPG_BIN | Central Pattern Generator (Binary) | Binary version of CPG |
| `o` | T_SIGNAL_GEN | Signal generator for joint movements | Joint motion signal generation |
| `.` | T_ACCELERATE | Accelerate current motion | `.` |
| `,` | T_DECELERATE | Decelerate current motion | `,` |

### Servo Calibration & Control

| Token | Name | Description | Example |
|-------|------|-------------|---------|
| `c` | T_SERVO_CALIBRATE | Enter calibration posture and adjust joint offsets | `c` - calibration posture<br>`c 0 7 1 -4 2 3 8 5` - set offsets |
| `a` | T_ABORT | Abort calibration changes | `a` |
| `s` | T_SAVE | Save current calibration to EEPROM | `s` |
| `j` | T_JOINTS | Query joint angles | `j` - all angles<br>`j 8` - angle of joint 8 |
| `f` | T_SERVO_FEEDBACK | Read servo position feedback (if supported) | `f` - all positions<br>`f 8` - position of servo 8 |
| `F` | T_SERVO_FOLLOW | Make other legs follow moved legs (teach mode) | `F` |

**Servo Feedback Sub-commands** (used with `f`):
- `fl` - Learn mode: record dragged positions
- `fr` - Replay learned positions
- `fF` - Enable follow mode
- `ff` - Disable follow mode

### Gyroscope & Balance

| Token | Name | Description | Example |
|-------|------|-------------|---------|
| `g` | T_GYRO | Toggle gyro function on/off | `g` - toggle gyro |
| `l` | T_BALANCE_SLOPE | Adjust balance slope for roll/pitch | `l 1 1` - default slopes<br>`l -1 2` - custom slopes |
| `t` | T_TILT | Tilt adjustment | `t` |

**Gyro Sub-commands** (used with `g`):
- `gU` - Enable gyro data updates
- `gu` - Disable gyro data updates
- `gB` - Enable gyro balancing
- `gb` - Disable gyro balancing
- `gF` - Increase gyroscope sampling frequency (fineness)
- `gf` - Reduce gyroscope sampling frequency
- `gc` - Calibrate IMU
- `gci` - Calibrate IMU immediately
- `gP` - Continuously print gyro data
- `gp` - Print gyro data once

### Sound Commands

| Token | Name | Description | Example |
|-------|------|-------------|---------|
| `b` | T_BEEP | Play notes (ASCII format) | `b 12 8 14 8 16 8 17 8 19 4` - melody<br>`b 3` - set volume to 3 (0-10)<br>`b` - toggle sound on/off |
| `B` | T_BEEP_BIN | Play notes (Binary format) | `B 12 8 14 8 16 8 17 8 19 4`<br>`B` - toggle sound on/off |
| `u` | T_MEOW | Play meow sound | `u` |

### Task Queue

| Token | Name | Description | Example |
|-------|------|-------------|---------|
| `q` | T_TASK_QUEUE | Queue commands with delays | `q k sit:1000>m 8 0:500>` - sit for 1s, then move joint 8 after 500ms |

### Configuration & System

| Token | Name | Description | Example |
|-------|------|-------------|---------|
| `n` | T_NAME | Customize Bluetooth device name | `n MyDog` - set name to "MyDog" (takes effect on next boot) |
| `w` | T_WIFI_INFO | Display WiFi information | `w` |
| `!` | T_RESET | Reset EEPROM birthmark and reboot | `!` |
| `?` | T_QUERY | Query system information | `?`<br>`?p` - query partition info |
| `h` | T_HELP_INFO | Hold loop to check printed info | `h` |
| `T` | T_TEMP | Execute last received skill data | `T` |
| `x` | T_LEARN | Learning mode | `x` |

### GPIO Control

| Token | Name | Description | Example |
|-------|------|-------------|---------|
| `R` | T_READ | Read pin value | `Ra` - analog read<br>`Rd` - digital read |
| `W` | T_WRITE | Write pin value | `Wa` - analog write<br>`Wd` - digital write |

### Joystick & Extensions

| Token | Name | Description | Example |
|-------|------|-------------|---------|
| `J` | T_JOYSTICK | Joystick control | `J` |
| `X` | T_EXTENSION | Extension module commands | `XS` - Grove Serial<br>`XA` - Voice module |

### Command Format Notes

- **ASCII vs Binary**: Commands ending in uppercase (e.g., `I`, `M`, `B`) use binary format for more efficient data transfer
- **Parameters**: Space-separated values after the token
- **Confirmation**: Robot echoes the token back upon successful execution
- **Chaining**: Use Task Queue (`q`) to chain multiple commands with timing
- **Sub-commands**: Some tokens (like `g`, `f`) accept sub-command characters for specific functions

### Common Command Sequences

```
# Calibrate servos
c 0 5 1 -3 8 7    # Set offsets for joints 0, 1, 8
s                  # Save calibration

# Play a behavior sequence
k sit             # Sit down
k pushup          # Do pushups
k up              # Stand up

# Queue commands
q k sit:2000>k stand:1000>k walk:5000>k sit:0>

# Gyro control
g                 # Toggle gyro on/off
gB                # Enable balancing
gc                # Calibrate IMU

# Manual joint control
i 0 45 1 -45      # Move head pan to 45°, tilt to -45°
m 8 30 9 30 10 30 11 30  # Move all leg joints sequentially

# Sound
b 12 8 14 8 16 8  # Play melody (note, duration pairs)
b 5               # Set volume to 5
```

### Token Definitions Location

All tokens are defined in [src/RoboDog.h:153-229](src/RoboDog.h#L153-L229) and processed in [src/reaction.h](src/reaction.h).

## Joystick Control (T_JOYSTICK)

The `T_JOYSTICK` (`J`) command provides mobile app control through a combination of virtual joystick and button interface. Implementation: [src/reaction.h:436-494](src/reaction.h#L436-L494)

### Overview

The joystick command processes binary data from the Petoi mobile app in two modes:
- **Button Mode**: String commands sent when app buttons are pressed
- **Joystick Mode**: Analog X/Y coordinates converted to directional commands

### Data Format

**Button Mode** (triggered when byte = -126/0x82):
```
J + [0x82] + "command string"
```
Example: `J<0x82>k walk` - stores "k walk" as button command

**Joystick Mode** (X/Y coordinates):
```
J + [X byte] + [Y byte]
```
- X, Y range: -127 to +127 (signed int8)
- Example: `J[50][0]` - joystick pushed right

### Dead Zones

To prevent drift when joystick is near center:
- **X-axis dead zone**: ±31 (center1 = 125/2/2)
- **Y-axis dead zone**: ±12 (center = 255/2/10)

Values within dead zone are treated as center (0).

### Direction Mapping

The joystick converts analog X/Y input into 8 discrete directions + center:

```
         Y > 12 (Up)
              ↑
    l    f    r
     \   |   /
  L ← · 0 · → R    (X > 31 right, X < -31 left)
     /   |   \
    L    D    R
              ↓
         Y < -31 (Down)
```

**Direction Commands** (joystickDirCmd array):

| Position | X Range | Y Range | Command | Meaning |
|----------|---------|---------|---------|---------|
| Center | -31 to 31 | -12 to 12 | (null) | Neutral/stopped |
| Forward | -31 to 31 | > 12 | `F` | Forward |
| Forward-Left | < -31 | > 12 | `f` | Forward-left |
| Left | < -31 | -12 to 12 | `L` | Left turn |
| Back-Left | < -31 | < -31 | `L` | Back-left |
| Backward | -31 to 31 | < -31 | `D` | Backward/Down |
| Back-Right | > 31 | < -31 | `R` | Back-right |
| Right | > 31 | -12 to 12 | `R` | Right turn |
| Forward-Right | > 31 | > 12 | `r` | Forward-right |

### Processing Flow

1. **Strip flush markers**: Remove leading `~` characters
2. **Detect mode**:
   - If byte = -126 (0x82): **Button mode** → store command string in `buttonCmd[20]` buffer
   - Else: **Joystick mode** → read X/Y coordinates
3. **Joystick processing**:
   - Read `currX` and `currY` from input bytes
   - Calculate direction indices:
     - `dirX`: -1 (left), 0 (center), +1 (right)
     - `dirY`: -2 (far down), -1 (down), 0 (center), +1 (up)
   - Look up direction command using: `dirMap[dirY + 2][dirX + 1]`
   - Get direction character from `joystickDirCmd[]` array
4. **Execute combined command**:
   - If button command exists, append joystick direction as suffix
   - Add to task queue: `tQueue->addTask(buttonCmd[0], buttonCmd + 1)`
   - Clear `buttonCmd` buffer
   - Delay 500ms (rate limiting)

### Direction Map Array

```cpp
byte dirMap[][3] = {
  { 11,  9, 10 },   // dirY = -2 (far down):    l, f, r
  {  8,  1,  2 },   // dirY = -1 (down):        L, F, R
  {  7,  0,  3 },   // dirY =  0 (center):      L, 0, R
  {  6,  5,  4 }    // dirY = +1 (up):          L, D, R
};

char joystickDirCmd[] = {
  '\0', 'F', 'R', 'R', 'R', 'D', 'L', 'L', 'L', 'f', 'r', 'l'
};
```

### Usage Examples

**Basic Joystick Movement**:
```
J + [X=0, Y=50]      → Forward (F)
J + [X=60, Y=0]      → Right turn (R)
J + [X=-60, Y=0]     → Left turn (L)
J + [X=0, Y=-60]     → Backward (D)
J + [X=40, Y=40]     → Forward-right (r)
J + [X=-40, Y=40]    → Forward-left (f)
J + [X=5, Y=5]       → Center (within dead zone, no action)
```

**Button + Joystick Combo**:
```
Step 1: J + [0x82] + "k walk"   → Store button command "k walk"
Step 2: J + [X=50, Y=0]         → Append 'R' → Execute "k walkR" (walk right variant)

Step 1: J + [0x82] + "k trot"   → Store button command "k trot"
Step 2: J + [X=0, Y=50]         → Append 'F' → Execute "k trotF" (trot forward)
```

### Key Features

- **Dead Zone Filtering**: Prevents unintended commands from joystick drift
- **Direction Quantization**: Converts smooth analog input to 8 discrete directions
- **Command Buffering**: Combines button selection with directional modifier
- **Rate Limiting**: 500ms delay prevents command flooding
- **Binary Protocol**: Efficient for mobile app Bluetooth communication

### Mobile App Integration

The joystick control is primarily designed for the **Petoi mobile app**:
- Virtual joystick sends continuous X/Y coordinate updates
- Buttons send skill/command strings
- Combined mode: Select skill with button, add direction with joystick
- Example: Press "walk" button → push joystick forward → robot walks forward

### Implementation Details

- **Token**: `T_JOYSTICK` = `'J'` ([src/RoboDog.h:194](src/RoboDog.h#L194))
- **Handler**: [src/reaction.h:436-494](src/reaction.h#L436-L494)
- **Buffer**: `char buttonCmd[20]` ([src/RoboDog.h:280](src/RoboDog.h#L280))
- **Primary Interface**: Petoi mobile app (Bluetooth)
- **Supported Modes**: Button commands, analog joystick, combined button+joystick
