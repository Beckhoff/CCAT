#Beckhoff CCAT FPGA
The CCAT FPGA is used inside Beckhoff Embedded PCs and Fieldbus Cards.
It is internally connected via a PC interface (usually PCIe) and offers interfaces for different 
Industrial Fieldbusses (e.g. EtherCAT®)
Because the device is not yet supported by the linux kernel and therefore not recognized as a standard 
Ethernet interface you need this driver to run an EtherCAT® master over this interface.
For further information about EtherCAT® please see links below:

http://www.ethercat.org/ <br>
http://infosys.beckhoff.com/english.php?content=../content/1033/ethercatsystem/html/bt_ethercatsystem_title.htm&id=7474

###Supported CCAT functions

- Ethernet (EtherCAT® Master)
- GPIO
- SRAM
- FPGA update

###Supported devices

- [CX50xx](http://infosys.beckhoff.com/english.php?content=../content/1033/cx5000_hw/1853842315.html&id=502)
- [CX51xx](http://infosys.beckhoff.com/english.php?content=../content/1033/cx51x0_hw/1853856523.html&id=574)
- [CX20xx](http://infosys.beckhoff.com/english.php?content=../content/1033/cx2000_hw/399078795.html&id=830)

###How to build and install the kernel modules:

1. cd into ccat <src_dir>
2. make && make install

###How to configure the driver:
All functions are implemented in a single kernel module. <br>
To disable some of the functions modify 'static const struct ccat_driver *const drivers[]' in 'module.c' according to your needs.
