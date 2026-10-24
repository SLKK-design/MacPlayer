/*
 * RootCert.h - Embedded CA certificates for the Spotify hosts (MacPlayer)
 *
 * Chain of trust for api.spotify.com and i.scdn.co:
 *   *.spotify.com / *.scdn.co  (leaf, RSA 2048, SHA256)
 *     -> DigiCert Global G2 TLS RSA SHA256 2020 CA1  (intermediate, RSA 2048)
 *       -> DigiCert Global Root G2  (root, RSA 2048)
 *
 * Chain of trust for accounts.spotify.com (token refresh), as of Oct 2026:
 *   accounts.spotify.com  (leaf)
 *     -> Certainly Intermediate R1  (intermediate)
 *       -> Starfield Root Certificate Authority - G2  (root)
 *
 * TLS 1.2 with ECDHE-RSA-AES128-GCM-SHA256 and P-256 key exchange.
 * RSA cert verification is handled natively by mbedTLS (no micro-ecc needed
 * for cert verification — only for the P-256 ECDHE key exchange itself).
 */

#pragma once
#include <stddef.h>

/* Definitions are in RootCert.cpp (one copy only), as in Claude-86 */

/* DigiCert Global Root G2
 * Subject: CN=DigiCert Global Root G2, O=DigiCert Inc, OU=www.digicert.com, C=US
 * Expires: Jan 15 12:00:00 2038 GMT
 */
extern const unsigned char kDigiCertGlobalRootG2_DER[];
extern const size_t        kDigiCertGlobalRootG2_DER_len;

/* DigiCert Global G2 TLS RSA SHA256 2020 CA1  (intermediate)
 * Subject: CN=DigiCert Global G2 TLS RSA SHA256 2020 CA1, O=DigiCert Inc, C=US
 * Expires: Mar 29 23:59:59 2031 GMT
 */
extern const unsigned char kDigiCertG2TLS_DER[];
extern const size_t        kDigiCertG2TLS_DER_len;

/* Starfield Root Certificate Authority - G2  (root for accounts.spotify.com)
 * Subject: CN=Starfield Root Certificate Authority - G2, O=Starfield Technologies, Inc., C=US
 * Expires: Dec 31 23:59:59 2037 GMT
 * SHA-256: 2C:E1:CB:0B:F9:D2:F9:E1:02:99:3F:BE:21:51:52:C3:B2:DD:0C:AB:DE:1C:68:E5:31:9B:83:91:54:DB:B7:F5
 */
extern const unsigned char kStarfieldRootG2_DER[];
extern const size_t        kStarfieldRootG2_DER_len;

/* Certainly Intermediate R1  (intermediate for accounts.spotify.com)
 * Subject: CN=Certainly Intermediate R1, O=Certainly, C=US
 * Issuer:  Starfield Root Certificate Authority - G2
 * Expires: Jun 21 23:59:59 2032 GMT
 * SHA-256: FE:C4:1E:32:CA:75:C2:95:A6:24:0F:A6:39:D3:AB:E3:BF:B5:CB:13:1D:66:90:E2:33:1A:17:6B:ED:2E:5B:D2
 */
extern const unsigned char kCertainlyR1_DER[];
extern const size_t        kCertainlyR1_DER_len;
