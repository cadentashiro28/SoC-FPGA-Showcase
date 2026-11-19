# SoC-FPGA-Showcase
A showcase of some of my contributions to firmware on Husky Robotics Instrumentation's Efinix Sapphire Ti375 SoC FPGA. Includes documentation of C-code, some RTL, and system integration decisions.

I'd recommend taking a look at the general overview, then the individual blocks. Thank you for looking at my work.
## General Overview:
<img width="1491" height="354" alt="Screenshot 2026-11-18 230448" src="https://github.com/user-attachments/assets/0cf2f76d-663b-466e-aca2-7c0d83c8dfc8" />                                        
1. camera.c: programs the PiCam's registers over I2C for a generic 1080p stream at 30FPS
   
2. Efinix camera_csi2_rx IP: outputs the camera's pixel stream, 10-bits per pixel, 4 pixels per clock
   
3. Multiple stages are handled by camera_path.sv
     - camera_write.sv: connects the camera IP to the FIFO IP
     - FIFO IP: holds frames in a buffer
     - camera_gearbox.sv: packs pixel data into bytes.
   
4. pdma.c: configures the video DMA channel to receive either a screenshot or a stream of video data

5. frame_tx: takes frames and chunks them into UDP sized packets to be sent over Ethernet 
   
This is a cursory glance at the camera data pipeline. Please look at the individual module's READMEs if you want
to learn more about implementation details and design decisions.
