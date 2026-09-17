#pragma once
#include <string>
#include <vector>
#include <cstdint>
// Password credential handling for the return-from-screensaver lock.
// In memory a verifier is "iterations:saltBase64:hashBase64:length". config.cpp
// encrypts it with the lock policy before writing; no plaintext password is saved.
// Hashing uses PBKDF2-HMAC-SHA256
// through the Windows CNG (bcrypt) primitives. The trailing length lets the lock
// screen stop input and submit by itself at the last character; credentials from
// older versions have no length and still verify.
namespace Lock {
constexpr uint32_t Iterations = 600000;
constexpr uint32_t SaltLength = 16;
constexpr uint32_t HashLength = 32;
std::wstring base64Encode(const std::vector<uint8_t>& data);
bool base64Decode(const std::wstring& text, std::vector<uint8_t>& out);
bool randomBytes(std::vector<uint8_t>& out, size_t count);
bool derive(const std::wstring& password, const std::vector<uint8_t>& salt,
            uint32_t iterations, uint32_t length, std::vector<uint8_t>& out);
bool constantTimeEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
// Builds a fresh credential from a password. Returns an empty string on failure.
std::wstring create(const std::wstring& password);
// Constant-time verification of a password against a stored credential.
bool verify(const std::wstring& password, const std::wstring& credential);
// Password length in UTF-16 units (what a page's value.length counts), or 0 when unknown.
size_t passwordLength(const std::wstring& credential);
// Adds the length to a credential that has none; anything else is returned unchanged.
std::wstring withLength(const std::wstring& credential, size_t length);
bool validCredential(const std::wstring& credential);
bool needsUpgrade(const std::wstring& credential);
// DPAPI encrypts the verifier and policy for the current Windows user; no key in the exe.
std::wstring protect(const std::wstring& data);
bool unprotect(const std::wstring& sealed, std::wstring& data);
void wipe(std::wstring& text);
void wipe(std::string& text);
constexpr size_t MaxPasswordLength = 128;
}
