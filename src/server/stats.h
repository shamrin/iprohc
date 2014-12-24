/* the same as standard in_addr, because CFFI .cdef doesn't support #include */
struct stats_in_addr {
   uint32_t s_addr;
};

struct udp_stats {
  /* most fields the same as in struct statitics */
  int decomp_failed;
  int decomp_total;

  int comp_failed;
  int comp_total;

  int head_comp_size;
  int head_uncomp_size;

  int total_comp_size;
  int total_uncomp_size;

  int unpack_failed;
  int total_received;

  int n_stats_packing;
  int stats_packing[16];

  struct stats_in_addr client_addr;
};
