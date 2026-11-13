# miniz inflate

Only the bounded DEFLATE decoder from miniz 3.0.2 is used.
Upstream: https://github.com/richgel999/miniz/tree/3.0.2
License: ../../licenses/miniz.txt (also retained in the source).

The only source change is miniz_tinfl.c including miniz_tinfl.h directly.
miniz_export.h defines an empty export macro for the static build.
Upstream SHA-256 hashes:

- miniz_tinfl.c: `9b7edc1fab708166e10f5e90a618a91c95a91496c4692e38160fdae810c1149a`
- miniz_tinfl.h: `040aad2d9ca23c487652c150277d950be12e72c5453ae1f7c6713ed2efd16b4e`
- miniz_common.h: `8b60e76e726dfcf41a338c3a4c54c70038041a21e6314aa54fd3b7f729ea3412`
