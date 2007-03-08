#ifndef __NDMA_H__
  #define __NDMA_H__

    #ifndef __ASSEMBLY__

// DMA Registers
typedef volatile struct
{
  int np_dmastatus;        // status register
  int np_dmareadaddress;   // read address
  int np_dmawriteaddress;  // write address
  int np_dmalength;        // length in bytes
  int np_dmareserved1;     // reserved
  int np_dmareserved2;     // reserved
  int np_dmacontrol;       // control register
  int np_dmareserved3;     // control register alternate
} np_dma;

// DMA Register Bits
enum
{
  np_dmacontrol_byte_bit  = 0, // Byte transaction
  np_dmacontrol_hw_bit    = 1, // Half-word transaction
  np_dmacontrol_word_bit  = 2, // Word transaction
  np_dmacontrol_go_bit    = 3, // enable execution
  np_dmacontrol_i_en_bit  = 4, // enable interrupt
  np_dmacontrol_reen_bit  = 5, // Enable read end-of-packet
  np_dmacontrol_ween_bit  = 6, // Enable write end-of-packet
  np_dmacontrol_leen_bit  = 7, // Enable length=0 transaction end
  np_dmacontrol_rcon_bit  = 8, // Read from a fixed address
  np_dmacontrol_wcon_bit  = 9, // Write to a fixed address
  np_dmacontrol_doubleword_bit = 10, // Double-word transaction
  np_dmacontrol_quadword_bit = 11, // Quad-word transaction
 
  np_dmastatus_done_bit   = 0, // 1 when done.  Status write clears.
  np_dmastatus_busy_bit   = 1, // 1 when busy.
  np_dmastatus_reop_bit   = 2, // read-eop received
  np_dmastatus_weop_bit   = 3, // write-eop received
  np_dmastatus_len_bit    = 4, // requested length transacted
 
  np_dmacontrol_byte_mask = (1 << 0), // Byte transaction
  np_dmacontrol_hw_mask   = (1 << 1), // Half-word transaction
  np_dmacontrol_word_mask = (1 << 2), // Word transaction
  np_dmacontrol_go_mask   = (1 << 3), // enable execution
  np_dmacontrol_i_en_mask = (1 << 4), // enable interrupt
  np_dmacontrol_reen_mask = (1 << 5), // Enable read end-of-packet
  np_dmacontrol_ween_mask = (1 << 6), // Enable write end-of-packet
  np_dmacontrol_leen_mask = (1 << 7), // Enable length=0 transaction end
  np_dmacontrol_rcon_mask = (1 << 8), // Read from a fixed address
  np_dmacontrol_wcon_mask = (1 << 9), // Write to a fixed address
  np_dmacontrol_doubleword_mask = (1 << 10), // Double-word transaction
  np_dmacontrol_quadword_mask = (1 << 11), // Quad-word transaction
 
  np_dmastatus_done_mask  = (1 << 0), // 1 when done.  Status write clears.
  np_dmastatus_busy_mask  = (1 << 1), // 1 when busy.
  np_dmastatus_reop_mask  = (1 << 2), // read-eop received
  np_dmastatus_weop_mask  = (1 << 3), // write-eop received
  np_dmastatus_len_mask   = (1 << 4), // requested length transacted
};

    #endif /* __ASSEMBLY__ */

#endif
/* End of File */
