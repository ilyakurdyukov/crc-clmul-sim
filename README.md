## CRC using simulated CLMUL

An experiment to replace the CRC algorithm using LUT with a code that simulates CLMUL (carry-less multiplication).

This is similar to the MCM (Multiple Constant Multiplication) problem, but for carry-less multiplication.

On modern CPUs this method is faster than code using LUT. But slower than using a real CLMUL instruction.

* Optimized for 64-bit architectures, slower on 32-bit architectures. The only 64-bit CPUs known to me without the CLMUL instruction are Elbrus before v6 (if not counting the ancient ones like Pentium 4).

Also in this benchmark there are variants using hardware CLMUL instruction (for x86 and AArch64).

### How to build and run

```bash
cc -Wall -Wextra -pedantic -march=native -O3 main.c -o main
for m in crc32_slice4 crc32_slice4 \
		crc32_clsim crc32_clsim \
		crc64_clmul crc64_clmul
		crc64_slice4 crc64_slice4 \
		crc64_clsim crc64_clsim \
		crc64_clmul crc64_clmul; do
	./main -t $m -l 100000000
done
```

* `x86`: use `-march=native` or `-msse4.2 -mpclmul`
* `arm`: use `-mcpu=native` or `-march=armv8-a+crypto+crc`

### List of available CRC variants:

`crc32/64_micro`: simplest implementation  
`crc32/64_simple`: LUT  
`crc32/64_slice4`: LUT, slice by 4  
`crc32/64_clsim`: CLMUL simulation  
`crc32/64_clmul`: using CLMUL instructions (x86, e2k-v6)  
`crc32/64_clmul2`: CLMUL simulation code, but using CLMUL instruction (e2k-v6, ARMv8, x86)  
`crc32_arm`: using CRC32 instructions (ARMv8)  
`crc32_arm_long`: same but with 4 instructions in parallel for 16KB blocks.  
`crc32_intel`: using CRC32 instructions, different polynomial (SSE4.2)  
`crc32_intel_long`: same but with 4 instructions in parallel for 16KB blocks.  

### Results

#### (x86_64) Alder Lake (P) 3.5GHz / GCC 11.2.0
```
e76a8c2e crc32_slice4: 79.364ms
e76a8c2e crc32_slice4: 79.339ms
e76a8c2e crc32_clsim: 53.848ms
e76a8c2e crc32_clsim: 53.997ms
e76a8c2e crc32_clmul: 10.771ms
e76a8c2e crc32_clmul: 10.781ms
552c62dd crc32_intel: 14.398ms
552c62dd crc32_intel: 14.360ms
552c62dd crc32_intel_long: 4.196ms
552c62dd crc32_intel_long: 4.193ms
85b023217b513aeb crc64_slice4: 72.965ms
85b023217b513aeb crc64_slice4: 72.995ms
85b023217b513aeb crc64_clsim: 49.685ms
85b023217b513aeb crc64_clsim: 49.671ms
85b023217b513aeb crc64_clmul: 10.755ms
85b023217b513aeb crc64_clmul: 10.761ms
```

#### (x86) Alder Lake (P) 3.5GHz / GCC 11.2.0
```
e76a8c2e crc32_slice4: 75.898ms
e76a8c2e crc32_slice4: 76.016ms
e76a8c2e crc32_clsim: 67.376ms
e76a8c2e crc32_clsim: 67.357ms
e76a8c2e crc32_clmul: 10.754ms
e76a8c2e crc32_clmul: 10.753ms
552c62dd crc32_intel: 21.528ms
552c62dd crc32_intel: 21.553ms
552c62dd crc32_intel_long: 7.787ms
552c62dd crc32_intel_long: 7.774ms
85b023217b513aeb crc64_slice4: 93.266ms
85b023217b513aeb crc64_slice4: 93.271ms
85b023217b513aeb crc64_clsim: 103.538ms
85b023217b513aeb crc64_clsim: 103.527ms
85b023217b513aeb crc64_clmul: 10.760ms
85b023217b513aeb crc64_clmul: 10.756ms
```

#### (x86_64) Alder Lake (P) 3.5GHz / Clang 13.0.0
```
e76a8c2e crc32_slice4: 86.163ms
e76a8c2e crc32_slice4: 86.062ms
e76a8c2e crc32_clsim: 54.239ms
e76a8c2e crc32_clsim: 54.494ms
e76a8c2e crc32_clmul: 9.650ms
e76a8c2e crc32_clmul: 9.637ms
552c62dd crc32_intel: 14.330ms
552c62dd crc32_intel: 14.564ms
552c62dd crc32_intel_long: 3.935ms
552c62dd crc32_intel_long: 3.941ms
85b023217b513aeb crc64_slice4: 79.391ms
85b023217b513aeb crc64_slice4: 79.411ms
85b023217b513aeb crc64_clsim: 50.678ms
85b023217b513aeb crc64_clsim: 50.565ms
85b023217b513aeb crc64_clmul: 9.429ms
85b023217b513aeb crc64_clmul: 9.411ms
```

#### (e2k-v6) Elbrus-16C 2.0GHz / LCC 1.25.20
```
e76a8c2e crc32_slice4: 183.338ms
e76a8c2e crc32_slice4: 184.790ms
e76a8c2e crc32_clsim: 94.326ms
e76a8c2e crc32_clsim: 94.325ms
e76a8c2e crc32_clmul: 25.223ms
e76a8c2e crc32_clmul: 25.246ms
85b023217b513aeb crc64_slice4: 173.764ms
85b023217b513aeb crc64_slice4: 173.633ms
85b023217b513aeb crc64_clsim: 59.746ms
85b023217b513aeb crc64_clsim: 59.756ms
85b023217b513aeb crc64_clmul: 25.184ms
85b023217b513aeb crc64_clmul: 25.186ms
```

#### (e2k-v5) Elbrus-8CB 1.5GHz / LCC 1.25.20
```
e76a8c2e crc32_slice4: 225.034ms
e76a8c2e crc32_slice4: 225.183ms
e76a8c2e crc32_clsim: 126.283ms
e76a8c2e crc32_clsim: 126.203ms
85b023217b513aeb crc64_slice4: 208.076ms
85b023217b513aeb crc64_slice4: 207.665ms
85b023217b513aeb crc64_clsim: 79.964ms
85b023217b513aeb crc64_clsim: 79.935ms
```

#### (e2k-v3) Elbrus-4C 0.75GHz / LCC 1.25.23
```
e76a8c2e crc32_slice4: 483.766ms
e76a8c2e crc32_slice4: 477.500ms
e76a8c2e crc32_clsim: 279.485ms
e76a8c2e crc32_clsim: 265.939ms
85b023217b513aeb crc64_slice4: 407.637ms
85b023217b513aeb crc64_slice4: 402.159ms
85b023217b513aeb crc64_clsim: 235.114ms
85b023217b513aeb crc64_clsim: 227.732ms
```

#### (AArch64) Allwinner H616, Cortex-A53 1.5GHz / GCC 9.3.0
```
e76a8c2e crc32_slice4: 225.161ms
e76a8c2e crc32_slice4: 224.922ms
e76a8c2e crc32_clsim: 150.267ms
e76a8c2e crc32_clsim: 150.975ms
e76a8c2e crc32_clmul: 62.793ms
e76a8c2e crc32_clmul: 67.103ms
e76a8c2e crc32_arm: 64.259ms
e76a8c2e crc32_arm: 62.714ms
e76a8c2e crc32_arm_long: 97.723ms
e76a8c2e crc32_arm_long: 98.681ms
85b023217b513aeb crc64_slice4: 192.621ms
85b023217b513aeb crc64_slice4: 192.354ms
85b023217b513aeb crc64_clsim: 166.035ms
85b023217b513aeb crc64_clsim: 166.419ms
85b023217b513aeb crc64_clmul: 62.427ms
85b023217b513aeb crc64_clmul: 62.634ms
```

#### (x86_64) Celeron-1007U, Ivy Bridge 1.5GHz / GCC 7.5.0
```
e76a8c2e crc32_slice4: 188.557ms
e76a8c2e crc32_slice4: 189.265ms
e76a8c2e crc32_clsim: 147.344ms
e76a8c2e crc32_clsim: 147.005ms
e76a8c2e crc32_clmul: 96.525ms
e76a8c2e crc32_clmul: 96.541ms
552c62dd crc32_intel: 33.676ms
552c62dd crc32_intel: 33.649ms
552c62dd crc32_intel_long: 11.794ms
552c62dd crc32_intel_long: 11.618ms
85b023217b513aeb crc64_slice4: 209.050ms
85b023217b513aeb crc64_slice4: 208.272ms
85b023217b513aeb crc64_clsim: 162.125ms
85b023217b513aeb crc64_clsim: 162.014ms
85b023217b513aeb crc64_clmul: 91.811ms
85b023217b513aeb crc64_clmul: 91.745ms
```

