ifeq ($(CONFIG_VITHAR_HWVER_R0P0),y)
obj-y			+= drm/ vga/ stub/ ion/ vithar/
else
obj-y			+= drm/ vga/ stub/ ion/ vithar_rev0/
endif

# does not work, R3P2 not set?
ifeq ($(CONFIG_MALI_UMP_R3P2),y)
obj-y           += mali400/r3p2/mali/
obj-y           += mali400/r3p2/ump/
endif
