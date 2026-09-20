############################################################################
# apps/examples/ok8mp_can_service/Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_OK8MP_CAN_SERVICE)
MAINSRC   = ok8mp_can_service_main.c
CSRCS     = ok8mp_can_isotp.c ok8mp_can_diag.c

include $(APPDIR)/Application.mk
