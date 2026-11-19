# SoC-FPGA-Showcase
A showcase of some of my contributions to firmware on Husky Robotics Instrumentation's Efinix Sapphire Ti375 SoC FPGA. Includes documentation of C-code, some RTL, and system integration decisions.

I'd recommend taking a look at the general overview, then each block in numbered order. Thank you for looking at my work.
## General Overview:

                                               FPGA (RTL)                                    Hardened RISC-V CPU (C)                                        
                                                                                               ┌────────┐                                                   
┌────────────────────────────────────────────────────────────────────────────────┬─────────────┤camera.c│ 1                    │                            
│                       2                           3                            │            4└────────┘  5                   │                            
│       ┌─────────────────────────────┐     ┌──────────────┐                     │        ┌──────┐   ┌───────────┐             │                            
└──────►│Raspberry Pi Camera─Module─V2┼────►│camera_path.sv├─────────────────────┼───────►│pdma.c┼──►│frame_tx.c ├────────────►│   Ethernet to Host Computer
        └─────────────────────────────┘     └───────┬──────┘                     │        └──────┘   └───────────┘             │                            
                                                    │                            │                                             │                            
                              ┌─────────────────────┼─────────────────────┐      │                                             │                            
                              │                     │                     │                                                                                 
                              │                     │                     │                                                                                 
                       camera_write.sv       Efinix FIFO IP        camera_gearbox.sv                                                                        

1) camera.c - programs the PiCam's registers over I2C for a generic 1080p stream at 30FPS
2) Efinix camera_csi2_rx IP outputs the camera's pixel stream, 10-bits per pixel, 4 pixels per clock
3) Multiple stages are handled by camera_path.sv
   - camera_write.sv: connects the camera IP to the FIFO IP
   - FIFO IP: holds frames in a buffer
   - camera_gearbox.sv: packs pixel data into bytes.
4) pdma.c: configures the video DMA channel to receive either a screenshot or a stream of video data
5) frame_tx: takes frames and chunks them into UDP sized packets to be sent over Ethernet

This is a cursory glance at the camera data pipeline. Please look at the individual module's READMEs if you want
to learn more about implementation details and design decisions.
