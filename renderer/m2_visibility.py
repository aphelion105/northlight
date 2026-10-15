"""Conservative proof of M2 draw batches that never reach an alpha threshold; never evaluates time.

MD20/264 opacity inputs of a batch (the game multiplies them in CM2Model::GetNumVisibleBatches and
GatherModelBatchesShadowMap; the weight only for textureCount > 0): colour-track alpha (header +72,
40 bytes per colour, alpha track at +20, batch index +8) and texture weight (header +88, lookup +144,
batch combo +20). A batch is dropped only when the product of each track's largest key over all
sequences stays under the threshold, so a fade that ever reaches it is kept. Missing/external/
unsupported tracks count as 1.0: they are NOT proof. Keep those batches for the existing extractor.
"""
import struct


def _array(data, offset, count, fmt):
    size = struct.calcsize(fmt)
    if count > 1000000 or offset < 0 or offset + count * size > len(data):
        raise ValueError('M2 visibility array bounds')
    return list(struct.iter_unpack(fmt, data[offset:offset + count * size]))


def track_max(data, track_offset):
    """Largest fixed16 key (0..1) of a present step/linear track over all sequences, or None when not provable."""
    try:
        kind, global_id, nt, ot, nk, ok = struct.unpack_from('<Hh4I', data, track_offset)
        animations = struct.unpack_from('<I', data, 28)[0]
        globals_count, globals_offset = struct.unpack_from('<II', data, 20)
        if kind not in (0, 1) or nt != nk or not nk:
            return None
        if global_id == -1:
            if nk != max(1, animations):
                return None
        elif 0 <= global_id < globals_count:
            _array(data, globals_offset, globals_count, '<I')
            if nk != 1:
                return None
        else:
            return None
        times = _array(data, ot, nt, '<II')
        keys = _array(data, ok, nk, '<II')
        peak = 0
        for (tc, to), (kc, ko) in zip(times, keys):
            if not tc or tc != kc:
                return None
            stamps = [v[0] for v in _array(data, to, tc, '<I')]
            if stamps != sorted(stamps):
                return None
            peak = max([peak] + [abs(v[0]) for v in _array(data, ko, kc, '<h')])
        return min(peak / 32767, 1.0)
    except (ValueError, struct.error):
        return None


def zero_weight(data, track_offset):
    """True only for present step/linear tracks entirely zero in all sequences."""
    return track_max(data, track_offset) == 0


def alpha_bound(data, batch):
    """Upper bound of colour alpha * texture weight over all keys; model alpha is 1 for a static placement."""
    try:
        bound = 1.0
        colors, color_offset = struct.unpack_from('<II', data, 72)
        if batch[5] < colors:
            peak = track_max(data, color_offset + batch[5] * 40 + 20)
            bound *= 1.0 if peak is None else peak
        if batch[8]:   # the weight multiplies only for a textured batch
            count, offset = struct.unpack_from('<II', data, 88)
            lookup_count, lookup_offset = struct.unpack_from('<II', data, 144)
            lookup = _array(data, lookup_offset, lookup_count, '<h')
            combo = batch[11]
            if combo != 0xffff and combo < len(lookup) and 0 <= lookup[combo][0] < count:
                peak = track_max(data, offset + lookup[combo][0] * 20)
                bound *= 1.0 if peak is None else peak
        return bound
    except (ValueError, struct.error, IndexError):
        return 1.0


def hidden_batches(data, batches, threshold=1e-4):
    """Return batch indices whose opacity is proven to stay below the threshold (the game's 1e-4 invisibility)."""
    try:
        if data[:4] != b'MD20' or struct.unpack_from('<I', data, 4)[0] != 264:
            return set()
        return {i for i, batch in enumerate(batches) if alpha_bound(data, batch) < threshold}
    except (ValueError, struct.error, IndexError):
        return set()
