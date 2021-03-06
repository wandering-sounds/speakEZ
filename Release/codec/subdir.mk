################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../codec/fsl_codec_adapter.c \
../codec/fsl_codec_common.c \
../codec/fsl_codec_i2c.c \
../codec/fsl_wm8960.c 

OBJS += \
./codec/fsl_codec_adapter.o \
./codec/fsl_codec_common.o \
./codec/fsl_codec_i2c.o \
./codec/fsl_wm8960.o 

C_DEPS += \
./codec/fsl_codec_adapter.d \
./codec/fsl_codec_common.d \
./codec/fsl_codec_i2c.d \
./codec/fsl_wm8960.d 


# Each subdirectory must supply rules for building sources it contributes
codec/%.o: ../codec/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DCPU_MIMXRT1011DAE5A -DCPU_MIMXRT1011DAE5A_cm7 -DFSL_RTOS_BM -DSDK_OS_BAREMETAL -DXIP_EXTERNAL_FLASH=1 -DXIP_BOOT_HEADER_ENABLE=1 -DSDK_DEBUGCONSOLE=0 -DCR_INTEGER_PRINTF -DPRINTF_FLOAT_ENABLE=0 -D__MCUXPRESSO -D__USE_CMSIS -DNDEBUG -D__REDLIB__ -I../drivers -I../CMSIS -I../usb/host/class -I../usb/host -I../component/serial_manager -I../device -I../usb/include -I../osa -I../usb/phy -I../codec -I../xip -I../component/i2c -I../utilities -I../component/uart -I../board -I../source -I../ -O3 -fno-common -g -Wall -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -D__REDLIB__ -fstack-usage -specs=redlib.specs -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


