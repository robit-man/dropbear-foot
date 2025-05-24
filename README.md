# DROPBEAR Foot Assembly and Codebase

[![Group 2](https://github.com/robit-man/dropbear-neck-assembly/assets/36677806/bd13c6f5-7a3f-4262-9891-4259f17abbe0)](https://t.me/fractionalrobots)

![foot](https://github.com/user-attachments/assets/4cb30ee8-01fa-418d-aa52-2bbe6a1f1e2b)

### HX711 × 4 → ESP32 DevKit v1 Pin Mapping

A search yielding [load cell kits](https://www.amazon.com/s?k=hx711+load+cell+kit+50kg), get 1 HX711 per load cell, or an [additional 6 pack of HX711 breakout boards](https://www.amazon.com/s?k=hx711+6pcs) note that most kits only give you a single HX711 per 4 load cell modules.

<img src="https://github.com/user-attachments/assets/36cb7a04-b46a-4b61-a50a-b9a473b187f2" height="250px">

<img src="https://github.com/user-attachments/assets/0aec55ac-66a9-493b-9099-e47a5e374abe" height="250px">


Now before you begin, please refer to [this guide](https://www.instructables.com/How-to-Convert-Your-HX-711-Board-From-10Hz-to-80Hz/) on which traces to cut and resolder to enable 80hz update rate on the HX711


A much simpler version of the above can be seen in this image (lift pin 15 and connect it to pin 16)

![Frame 33(1)](https://github.com/user-attachments/assets/de597161-ad6b-466a-95cc-b092bb2c112e)

![Frame 32](https://github.com/user-attachments/assets/8f02203e-7369-4469-9507-b84f87f9092d)


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
>   • **White (Excitation +) → HX711 E+**
>   • **Black (Excitation –) → HX711 E-**
>   • **Red (Signal) → HX711 A- (A+ is internally tied to E+ via 1k resistor as a reference signal against A-)**
>   • **Verify signal wire by checking resistance between all wire pairs**
>   • **Signal will be 1kOhm between each exitation wire, and exitation +- will be 2kOhm**

All four HX711 breakout boards share the same clock (SCK ↔ GPIO 4) while each has its own DOUT pin, allowing the sketch you provided to poll them independently at full speed.


interface.py frontend, click on pads to toggle index selection, and apply -100 pressure for 2 seconds to assign

![image](https://github.com/user-attachments/assets/15de8a93-ef5b-4daa-8859-15022774cf08)
