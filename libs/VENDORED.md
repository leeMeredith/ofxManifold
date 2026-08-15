# Vendored dependencies

## glm

- Upstream: https://github.com/g-truc/glm
- Version: 1.0.1
- Tag: `1.0.1`
- Path: `libs/glm`

Vendored rather than submoduled. GitHub ZIP downloads and openFrameworks
Project Generator users receive empty directories where submodules should be,
which makes an addon look broken on first use through no fault of the consumer.

Note that openFrameworks 0.12.1 already ships glm at `libs/glm/include`. This
copy exists so the kernel test suite builds with nothing but a compiler and
`make`, with no openFrameworks checkout on the machine or in CI. Consumers
building the addon inside an openFrameworks project use oF's copy; the two are
the same library.

To update: replace `libs/glm`, run `make test`, and record the new tag here.
