# LCD HD44780

Controls [LCD HD44780](../../../../docs/HD44780.pdf).

## Configuration
Configuration parameters in params.txt file:
```
LCD_CONFIG   <address>;<sda gpio pin>
```
Where `<address>` is the I2C address of the LCD, usually 0x27 or 0x3F; `<sda gpio pin>` is the Raspberry PIN where the SDA of the LCD is attached.
The SCL of the sensor must be attached next to SDA: `<sda gpio pin>` + 1.

Example configuration of LCD attached to SDA;SCL - GPIO8;GPIO9:
```
LCD_CONFIG	            0x27;8
```

## API
```
int lcd_clear_cell(int cell);
int lcd_set_text(int cell, int row, int column, char *text);
int lcd_set_double(int cell, int row, int column, double num);
int lcd_set_int(int cell, int row, int column, int num);
```

