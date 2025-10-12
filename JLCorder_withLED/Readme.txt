
These Files are to be used directly on https://jlcpcb.com/

1- Upload the esp_bmi_withLED_GBR.zip in Add gerber file
2- Select wanted PCB options - this is a 2-layer PCB
3- If you wan to add th component assembly, select PCB Assembly and tick Assemble top side
4- Click next twice
5- In section: Bill of Materials, add BOM file: esp_bmi270_mlx90393_withLED and CPL file: esp_bmi270_mlx90393-pos_numbered_withLED.

Note: The Xiao ESP32-C3 is added to the CPL but is not part of the PCB, a window "Project has unselected parts" will show when pressing Next, select "Do not place".
In the window component placement, make sure the dots of each component is aligned with the one on the PCB.

Note2: I the case of a component out of stock, look for an alternative on https://jlcpcb.com/parts, make sure the foot print is the same and modify the CPL file with the corresponding JLCPCB Part #.
