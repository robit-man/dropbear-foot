# DROPBEAR Foot Assembly and Codebase

### HX711 × 4 → ESP32 DevKit v1 Pin Mapping

![image](https://github.com/user-attachments/assets/36cb7a04-b46a-4b61-a50a-b9a473b187f2)

Now before you begin, please refer to [this guide](https://www.instructables.com/How-to-Convert-Your-HX-711-Board-From-10Hz-to-80Hz/) on which traces to cut and resolder to enable 80hz update rate on the HX711

A much simpler version of the above can be seen in this image (lift pin 15 and connect it to pin 16)
![image](https://github.com/user-attachments/assets/0aec55ac-66a9-493b-9099-e47a5e374abe)

| HX711 board     | HX711 pin | ESP32 pin   | Notes                                     |
| --------------- | --------- | ----------- | ----------------------------------------- |
| **All modules** | **VCC**   | **3 V3**    | 3.3 V supply (do **not** use 5 V)         |
|                 | **GND**   | **GND**     | Common ground with ESP32 & load cells     |
|                 | **SCK**   | **GPIO 4**  | **Shared clock** line for all four HX711s |
| **Load-cell 0** | **DOUT**  | **GPIO 32** | Independent data line                     |
| **Load-cell 1** | **DOUT**  | **GPIO 12** | Independent data line                     |
| **Load-cell 2** | **DOUT**  | **GPIO 13** | Independent data line                     |
| **Load-cell 3** | **DOUT**  | **GPIO 15** | Independent data line                     |

> **Load-cell wiring reminder (typical 3-wire):**
>   • **Red (Excitation +) → HX711 E+**
>   • **Black (Excitation –) → HX711 E-**
>   • **White (Signal) → HX711 A- (A+ is internally tied to E+)**

All four HX711 breakout boards share the same clock (SCK ↔ GPIO 4) while each has its own DOUT pin, allowing the sketch you provided to poll them independently at full speed.


![image](https://github.com/user-attachments/assets/bbf63001-aa33-432f-95fc-1649eef8e68c)

interface.py frontend, click on pads to toggle index selection, and apply -100 pressure for 2 seconds to assign

![image](https://github.com/user-attachments/assets/15de8a93-ef5b-4daa-8859-15022774cf08)
