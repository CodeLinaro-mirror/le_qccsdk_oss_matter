/*
 * mbedtls_rsa_get_bitlen shim for QCC730 HW crypto.
 *
 * libcryptoqcc730.a(rsa_alt.c.obj) provides the RSA hardware implementation
 * but was built against an older mbedTLS version that did not have
 * mbedtls_rsa_get_bitlen() (added in mbedTLS 3.x). The current Zephyr
 * mbedTLS (3.x) calls this function from pk_wrap.c when MBEDTLS_RSA_C=y.
 *
 * This shim provides the missing function by delegating to
 * mbedtls_rsa_get_len() (which the blob does provide) and converting
 * byte length to bit length. The implementation matches rsa.c exactly:
 *   return mbedtls_mpi_bitlen(&ctx->N);
 * which equals mbedtls_rsa_get_len(ctx) * 8 for a normalised RSA context.
 *
 * Matter SDK never calls RSA operations directly, so this shim is only
 * reachable via pk_wrap.c infrastructure — safe to implement as shown.
 */

#include "mbedtls/rsa.h"

size_t mbedtls_rsa_get_bitlen(const mbedtls_rsa_context *ctx)
{
    return mbedtls_rsa_get_len(ctx) * 8;
}
