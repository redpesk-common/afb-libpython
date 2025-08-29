# Changelog

All notable changes to this project will be documented in this file.

## [2.2.0] - 2025-08-29

### Changed

- **Breaking change** Verb arguments in callsync and callasync are not
  converted anymore to AFB JSONC, but to their AFB equivalent
  type. This means, e.g., strings that were converted to a JSONC
  object containing only a string are now converted to a STRINGZ.

## [2.1.3] - 2025-04-29

