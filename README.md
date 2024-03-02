# Beckhoff CCAT FPGA
The CCAT FPGA is used inside Beckhoff Embedded PCs and Fieldbus Cards.
It is internally connected via a PC interface (usually PCIe) and offers interfaces for different 
Industrial Fieldbusses (e.g. EtherCAT®)
Because the device is not yet supported by the linux kernel and therefore not recognized as a standard 
Ethernet interface you need this driver to run an EtherCAT® master over this interface.
For further information about EtherCAT® please see links below:

https://www.ethercat.org/ <br>
https://infosys.beckhoff.com/english.php?content=../content/1033/ethercatsystem/index.html

### Supported CCAT functions

- Ethernet (EtherCAT® Master)
- GPIO
- SRAM
- FPGA update
- ESC (EtherCAT@ Slave)
- IRQ

### Supported devices

- [CX50xx](https://www.beckhoff.com/CX5000/)
- [CX51xx](https://www.beckhoff.com/CX5100/)
- [CX20xx](https://www.beckhoff.com/CX2000/)
- [FC1121](https://www.beckhoff.com/en-en/products/ipc/pcs/accessories/fc1121.html)

### How to build and install the kernel modules:

1. cd into ccat <src_dir>
2. make && make install

### How to configure the driver:
All functions are implemented in a single kernel module. <br>
To disable some of the functions modify 'static const struct ccat_driver *const drivers[]' in 'module.c' according to your needs.
