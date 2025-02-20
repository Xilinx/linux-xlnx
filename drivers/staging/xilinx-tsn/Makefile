# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Xilinx network device drivers.
#

obj-$(CONFIG_XILINX_TSN) :=xilinx_emac_tsn.o
xilinx_emac_tsn-objs := xilinx_axienet_main_tsn.o xilinx_axienet_mcdma_tsn.o xilinx_axienet_mdio_tsn.o xilinx_tsn_ep.o xilinx_tsn_ep_ex.o xilinx_tsn_ip.o
xilinx_emac_tsn-$(CONFIG_XILINX_TSN_PTP) += xilinx_tsn_ptp_xmit.o xilinx_tsn_ptp_clock.o
xilinx_emac_tsn-$(CONFIG_XILINX_TSN_QBV) += xilinx_tsn_shaper.o
xilinx_emac_tsn-$(CONFIG_XILINX_TSN_QCI) += xilinx_tsn_qci.o
xilinx_emac_tsn-$(CONFIG_XILINX_TSN_CB) += xilinx_tsn_cb.o
xilinx_emac_tsn-$(CONFIG_XILINX_TSN_SWITCH) += xilinx_tsn_switch.o xilinx_tsn_switchdev.o
xilinx_emac_tsn-$(CONFIG_XILINX_TSN_QBR) += xilinx_tsn_preemption.o
xilinx_emac_tsn-$(CONFIG_AXIENET_HAS_TADMA) += xilinx_tsn_tadma.o
