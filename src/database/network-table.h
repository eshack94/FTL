/* Pi-hole: A black hole for Internet advertisements
*  (c) 2019 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  pihole-FTL.db -> network tables prototypes
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */
#ifndef NETWORKTABLE_H
#define NETWORKTABLE_H

bool create_network_table(void);
bool create_network_addresses_table(void);
void parse_neighbor_cache(void);
void updateMACVendorRecords(void);
bool unify_hwaddr(void);
char* getDatabaseHostname(const char* ipaddr) __attribute__((malloc));

#endif //NETWORKTABLE_H
