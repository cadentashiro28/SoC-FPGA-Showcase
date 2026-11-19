## AXI Slave Module

This AXI slave firmware was the first piece of code I wrote for the system. It is a great explanation of how the SoC functions with memory-mapped input output (MMIO).

The idea behind MMIO is that we can write to the AXI bus at the AXI's control register in memory, and read from the bus at its status register.

The Efinity AXI Slave IP was instantiated and wired up in our design by a colleague. We ran a simple loopback write / read test where we wrote a sequence, detected the sequence in fabric,
and altered the status register to a return sequence, then read it from the CPU.

