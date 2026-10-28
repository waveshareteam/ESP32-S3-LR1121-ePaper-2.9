# Repository configuration

`ci.json` is the machine-readable source for the product name, hardware
revision, ESP target, framework versions, and Arduino board options used by
repository automation.

`source_only_examples` lists exact repository-relative paths excluded from all CI
build matrices and release firmware packages. It currently covers LoRaWAN examples
10–13 in both frameworks, whose IDs and keys must be configured before local builds.
The inventory still checks all 13 source examples per framework; CI builds 01–09.
Both matrix discovery and release validation use this list, and unknown paths fail
validation so renamed examples cannot silently lose their exclusion.

The framework matrix is based on stable upstream releases and the current
example metadata. It must be confirmed by CI before being described as build
validated.