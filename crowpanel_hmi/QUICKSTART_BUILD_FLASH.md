# 🚀 Quick Start: Build & Flash Your CrowPanel with ESP-IDF

## Prerequisites ✓
- ✅ ESP-IDF installed at: `F:\esp-idf`
- ✅ VSCode with ESP-IDF extension installed
- ✅ Python 3.8+ (usually installed with ESP-IDF)
- ✅ ESP32-P4 CrowPanel connected to your PC

---

## Step 1: Verify ESP-IDF Installation

```powershell
# In PowerShell, check if idf.py works:
$env:IDF_PATH = "F:\esp-idf"
python "$env:IDF_PATH\tools\idf.py" --version
```

You should see: `ESP-IDF v5.x.x`

---

## Step 2: Open Project in VSCode

```powershell
cd "c:\Users\vlad\OneDrive\Documents\IrigationSystemFull\IOT\crowpanel_hmi"
code .
```

---

## Step 3: Configure ESP-IDF Extension (First Time Only)

1. Press `Ctrl+Shift+P` in VSCode
2. Search: `ESP-IDF: Configure ESP-IDF Extension`
3. Set **IDF_PATH** to: `F:\esp-idf`
4. Select Python executable (usually auto-detected)
5. Wait for configuration to complete

---

## Step 4: Set Build Target

In VSCode Terminal, run:

```bash
idf.py set-target esp32p4
```

Expected output:
```
Running cmake in build directory...
Target is now esp32p4.
```

---

## Step 5: Build the Project

Option A - Using VSCode buttons:
1. Click **ESP-IDF** button in VSCode sidebar
2. Click **Build** button

Option B - Using command line:
```bash
idf.py build
```

Expected output:
```
[100%] Built target app
Created app binary: .pio/build/esp32p4/app.bin
```

---

## Step 6: Find Your COM Port

```powershell
# List available COM ports:
[System.IO.Ports.SerialPort]::GetPortNames()
```

Look for your CrowPanel device (usually `COM3`, `COM4`, or `COM5`)

---

## Step 7: Flash to Device

Ensure:
- CrowPanel is powered ON
- USB cable is connected
- COM port is identified

Then run:

```bash
idf.py -p COM3 flash monitor
```

Replace `COM3` with your actual COM port.

Expected output:
```
Chip SHA256 Revision: 0
Verifying file...
[===] Verifying (100%)

--- idf_monitor on COM3 115200 ---
ets Jun  8 2016 00:22:57 rst:0x1 (PWDN_RESET),boot:0x8 (SPI_FAST_BOOT)
I (0) cpu_start: Starting scheduler...
I (40) CROWPANEL_MAIN: CrowPanel 7" HMI Starting (ESP32-P4)
I (100) DISPLAY_DRIVER: Initializing CrowPanel 7" display
```

---

## Step 8: Stop Monitoring

Press `Ctrl+]` to exit the monitor and return to VSCode terminal

---

## 🐛 Troubleshooting

### Port is already in use
```bash
# Kill any processes using the port:
Get-Process | Where-Object {$_.Handles -like "*COM3*"} | Stop-Process
```

### "Cannot find Python"
Make sure Python is in PATH:
```bash
python --version
```

If not found, run ESP-IDF installer again to add to PATH

### "Build failed with errors"
Check the error message - usually missing dependencies or incorrect includes. 

Common fixes:
```bash
# Clean build
idf.py fullclean
idf.py build

# Reconfigure
idf.py reconfigure
```

### Device not detected
```bash
# List USB devices:
Get-PnpDevice -Class Ports
```

Look for your CrowPanel's USB device name

---

## 📊 Next Steps After Flashing

1. **Monitor UART Output**: Terminal shows real-time logs
2. **Touch the Display**: If touch works, you'll see events in the log
3. **Check LVGL Display**: Verify the "CrowPanel 7\" HMI" splash screen appears

---

## 📝 Important Project Files

| File | Purpose |
|------|---------|
| `main/main.cpp` | Entry point, UART & display init |
| `main/display_driver.cpp` | LVGL + display driver framework |
| `main/ui.cpp` | User interface logic |
| `CMakeLists.txt` | Build configuration |
| `sdkconfig` | ESP-IDF settings |
| `partitions_16mb.csv` | Flash memory layout |

---

## 🔗 Useful Commands

```bash
# Full rebuild
idf.py fullclean && idf.py build

# Build and flash only (no monitor)
idf.py build && idf.py -p COM3 flash

# Erase flash entirely
idf.py -p COM3 erase-flash

# Read device info
idf.py -p COM3 chip-id

# Adjust settings interactively
idf.py menuconfig
```

---

**Ready to build?** Go to Step 4! 🎯
