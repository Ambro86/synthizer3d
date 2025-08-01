# Operations Common to All Sources

## Constructors

None.

## Properties

Enum | Type | Default | Range | Description
--- | --- | --- | --- | ---
SYZ_P_GAIN | double | Any double > 0 | An additional gain factor applied to this source.
SYZ_P_FILTER | biquad | identity | any | A filter which applies to all audio leaving the source, before `SYZ_P_FILTER_DIRECT` and `SYZ_P_FILTER_EFFECTS`.
SYZ_P_FILTER_DIRECT | biquad | identity | any | A filter which applies after `SYZ_P_FILTER` but not to audio traveling to effect sends.
SYZ_P_FILTER_EFFECTS | biquad | identity | any | A filter which runs after `SYZ_P_FILTER` but only applies to audio traveling through effect sends.

## Functions

### `syz_sourceAddGenerator`, `syz_sourceRemoveGenerator`

```
SYZ_CAPI syz_ErrorCode syz_sourceAddGenerator(syz_Handle source, syz_Handle generator);
SYZ_CAPI syz_ErrorCode syz_sourceRemoveGenerator(syz_Handle source, syz_Handle generator);
```

Add/remove a generator from a source. Each generator may be added once and
duplicate add calls will have no effect. Each generator should only be used with
one source at a time.

## Remarks

Sources represent audio output.  They combine all generators connected to them,
apply any effects if necessary, and feed the context. Subclasses of Source add
panning and other features.


All sources offer filters via `SYZ_P_FILTER`, `SYZ_P_FILTER_DIRECT` and
`SYZ_P_FILTER_EFFECTS`. First, `SYZ_P_FILTER` is applied, then the audio is
split into two paths: the portion heading directly to the speakers gets
`SYZ_P_FILTER_DIRECT`, and the portion heading to the effect sends gets
`SYZ_P_FILTER_EFFECTS`.  This can be used to simulate occlusion and perform
other per-source effect customization.
