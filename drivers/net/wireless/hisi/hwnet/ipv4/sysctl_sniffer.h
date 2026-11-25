/* mainline compat stub for the vendor Huawei hwnet sniffer helper */
#ifndef _HWNET_IPV4_SYSCTL_SNIFFER_H
#define _HWNET_IPV4_SYSCTL_SNIFFER_H

static inline void proc_sniffer_write_file(const unsigned char *hdr, unsigned int hdr_len,
                                           const unsigned char *frame, unsigned int frame_len,
                                           unsigned int dir)
{
    (void)hdr; (void)hdr_len; (void)frame; (void)frame_len; (void)dir;
}

#endif
