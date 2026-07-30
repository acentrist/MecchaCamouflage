#pragma once

#include <meccha/common/hash.hpp>

namespace meccha::launcher
{
using common::FileHash;
using common::HashError;
using common::HashErrorCode;
using common::Sha256Digest;
using common::parse_sha256_hex;
using common::sha256_bytes;
using common::sha256_file;
using common::sha256_hex;
} // namespace meccha::launcher
