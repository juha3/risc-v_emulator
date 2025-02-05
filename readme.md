# Simple RISC-V emulator

## Usage

``` ./rv32_emu <binary> ```

## Features

* Emulation starts at address 0x0
* Supports compressed instructions

## Example

```
$ ./rv32_emu progmem1k.bin |head -30
risc-v emulator
0x0000: 00002137    lui x2, 8192
0x0004: 1171        addi x2, x2, -4
0x0006: a011        J 0xa (imm = 4)
0x000a: 4081        li x1, 0
0x000c: 4181        li x3, 0
0x000e: 4201        li x4, 0
0x0010: 4281        li x5, 0
0x0012: 4301        li x6, 0
0x0014: 4381        li x7, 0
0x0016: 4401        li x8, 0
0x0018: 4481        li x9, 0
0x001a: 4501        li x10, 0
0x001c: 4581        li x11, 0
0x001e: 4601        li x12, 0
0x0020: 4681        li x13, 0
0x0022: 4701        li x14, 0
0x0024: 4781        li x15, 0
0x0026: 4801        li x16, 0
0x0028: 4881        li x17, 0
0x002a: 4901        li x18, 0
0x002c: 4981        li x19, 0
0x002e: 4a01        li x20, 0
0x0030: 4a81        li x21, 0
0x0032: 4b01        li x22, 0
0x0034: 4b81        li x23, 0
0x0036: 4c01        li x24, 0
0x0038: 4c81        li x25, 0
0x003a: 4d01        li x26, 0
0x003c: 4d81        li x27, 0
```
