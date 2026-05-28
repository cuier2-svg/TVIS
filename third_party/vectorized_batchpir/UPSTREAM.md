# Vectorized BatchPIR

Source: https://github.com/mhmughees/vectorized_batchpir.git

This directory contains the Vectorized BatchPIR implementation used by the
`batchpir` protocol wrapper under `src/batchpir`.

The integration builds the upstream client/server implementation as a static
library and excludes the upstream benchmark `src/main.cpp`, so this project can
choose the protocol from its own `src/main.cpp`.
