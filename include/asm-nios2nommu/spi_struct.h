// SPI Registers
typedef volatile struct
  {
  int np_spirxdata;       // Read-only, 1-16 bit
  int np_spitxdata;       // Write-only, same width as rxdata
  int np_spistatus;       // Read-only, 9-bit
  int np_spicontrol;      // Read/Write, 9-bit
  int np_spireserved;     // reserved
  int np_spislaveselect;  // Read/Write, 1-16 bit, master only
  int np_spiendofpacket;  // Read/write, same width as txdata, rxdata.
  } np_spi;

// SPI Status Register Bits
enum
  {
  np_spistatus_eop_bit  = 9,
  np_spistatus_e_bit    = 8,
  np_spistatus_rrdy_bit = 7,
  np_spistatus_trdy_bit = 6,
  np_spistatus_tmt_bit  = 5,
  np_spistatus_toe_bit  = 4,
  np_spistatus_roe_bit  = 3,

  np_spistatus_eop_mask  = (1 << 9),
  np_spistatus_e_mask    = (1 << 8),
  np_spistatus_rrdy_mask = (1 << 7),
  np_spistatus_trdy_mask = (1 << 6),
  np_spistatus_tmt_mask  = (1 << 5),
  np_spistatus_toe_mask  = (1 << 4),
  np_spistatus_roe_mask  = (1 << 3),
  };

// SPI Control Register Bits
enum
  {
  np_spicontrol_sso_bit   = 10,
  np_spicontrol_ieop_bit  = 9,
  np_spicontrol_ie_bit    = 8,
  np_spicontrol_irrdy_bit = 7,
  np_spicontrol_itrdy_bit = 6,
  np_spicontrol_itoe_bit  = 4,
  np_spicontrol_iroe_bit  = 3,

  np_spicontrol_sso_mask   = (1 << 10),
  np_spicontrol_ieop_mask  = (1 << 9),
  np_spicontrol_ie_mask    = (1 << 8),
  np_spicontrol_irrdy_mask = (1 << 7),
  np_spicontrol_itrdy_mask = (1 << 6),
  np_spicontrol_itoe_mask  = (1 << 4),
  np_spicontrol_iroe_mask  = (1 << 3),
  };

// SPI Routines.
int nr_spi_rxchar(np_spi *spiBase);
int nr_spi_txchar(int i, np_spi *spiBase);


