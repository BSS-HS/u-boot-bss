# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2026 BSS
#
# PDM-600 carrier board for the PHYTEC phyCORE-i.MX8MP SOM.

obj-y += imx8mp-pdm600.o

obj-y += ../../phytec/common/phytec_som_detection.o
obj-y += ../../phytec/common/phytec_som_detection_blocks.o
obj-y += ../../phytec/common/imx8m_som_detection.o

ifdef CONFIG_XPL_BUILD
obj-y += ../../phytec/phycore_imx8mp/spl.o
obj-$(CONFIG_IMX8M_LPDDR4) += ../../phytec/phycore_imx8mp/lpddr4_timing.o
endif
