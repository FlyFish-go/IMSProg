WCH CH341/CH347 parallel/SPI/I2C driver package (ch341wdm.inf)
===============================================================

Version: DriverVer 04/14/2025, 2.6.2025.04
Publisher: Nanjing Qinheng Microelectronics (WCH), https://www.wch.cn
Signature: Microsoft Windows Hardware Compatibility Publisher (CH341WDM.CAT)
Devices: CH341A/B (1A86:5512, 4348:5512), CH347T (1A86:55DB MI_02),
         CH347 (1A86:55DD MI_02), CH347F (1A86:55DE MI_04), CH339W (1A86:55E7 MI_02)

The files are the unmodified driver package of WCH's CH341PAR installer
(https://www.wch.cn/downloads/CH341PAR_EXE.html), exported from the Windows
driver store with "pnputil /export-driver". WCH describes CH341PAR as a
driver "for distribution with products to end users". Modifying any file
invalidates the catalog signature.

IMSProg copies this folder to driver\ in the Windows bundle. Programmer >
Driver... installs it with "pnputil /add-driver ch341wdm.inf /install".
IMSProg loads CH341DLLA64.DLL / CH347DLLA64.DLL from this folder or from
System32 at run time; it does not link against them.

SHA-256: see SHA256SUMS.txt.
