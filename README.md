# R5-4.4-kernel-source
LNX.LA.3.7.3.c2-03200-8939.0


More info:

1. This kernel source is compatible with some other OPPO devices(MSM8939 platform).

2. For R5, 

     a. project name: OPPO_14005(Chinese version) ; OPPO_14061(International version)
   
                      
          for example: 
          (android/kernel/drivers/power/oppo/oppo_bq24196.c)
          if(is_project(OPPO_14005)){
          .....
          }
     b. dts path: /arch/arm/boot/dts/14005
3. OPPO's makefile: using "./mk MSM_14005 new_module" to compile.
