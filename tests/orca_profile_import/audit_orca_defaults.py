"""Trace effective imported snapshots to explicit profiles, captured defaults or PS3 fallback."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
from compare_prusa_sources import absent, compact, flatten, profile_index, read_default_snapshot


def unmapped_source_fields(sources, defaults, effective_keys, aliases):
    """Retain explicit-only fields as well as omitted engine defaults.

    Absence of a direct PS3 key does not prove absence of an adapter. These rows
    are review candidates, with source/default evidence kept separate.
    """
    result = {}
    for kind, fields in sources.items():
        rows = {}
        for key in sorted(fields.keys() | defaults[kind].keys()):
            if aliases.get(key, key) in effective_keys:
                continue
            entry = fields.get(key)
            explicit = entry is not None and not absent(entry['value'])
            rows[key] = {
                'origin': 'explicit_source' if explicit else (
                    'orca_engine_default' if key in defaults[kind] else 'unset_source'),
                'value': entry['value'] if explicit else defaults[kind].get(key),
                'explicit_source': entry if explicit else None,
                'nil_source': entry if entry is not None and not explicit else None,
                'has_engine_default': key in defaults[kind],
                'engine_default': defaults[kind].get(key),
            }
        result[kind] = rows
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshots', type=Path)
    parser.add_argument('--orca-profiles', type=Path, required=True)
    parser.add_argument('--vendor', default='Snapmaker', help='Orca vendor manifest ID (default: Snapmaker)')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base = Path(__file__).resolve().parents[2] / 'src/slic3r-shared/src/Slic3r/Biz/Preset/IO'
    aliases = dict(re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', (base/'OrcaProfileMappings.inc').read_text().split('static const std::set')[0]))
    defaults = read_default_snapshot(base/'OrcaProfileDefaults.inc')
    index = profile_index(args.orca_profiles, 'OrcaFilamentLibrary') if (args.orca_profiles/'OrcaFilamentLibrary.json').exists() else {}
    index.update(profile_index(args.orca_profiles, args.vendor))
    snapshots = json.loads(args.snapshots.read_text())
    report = {'engine_default_evidence': defaults['evidence'], 'snapshots': {}}
    md = ['# Imported Orca default audit', '',
          'Explicit inherited profile values and captured engine defaults are separate sources. '
          'A PS3 fallback is not assumed to be an Orca default. Converted values do not by themselves prove equivalent algorithms.', '']
    for name, snap in snapshots.items():
        sources = {kind: flatten(index, kind, selected) for kind, selected in (
            ('machine', snap['name']), ('process', snap['process_name']), ('filament', snap['filament_name']))}
        emitted = snap['converted_inputs']
        fields = {}
        for key, value in snap['effective'].items():
            inputs = {group: values[key] for group, values in emitted.items() if key in values}
            explicit, engine, nil = [], [], []
            for kind in ('machine', 'process', 'filament'):
                for source_key in sorted(sources[kind].keys() | defaults['defaults'][kind].keys()):
                    target = aliases.get(source_key, source_key)
                    if target != key and not (source_key=='initial_layer_infill_speed' and key=='first_layer_solid_infill_speed') \
                        and not (source_key=='ironing_type' and key=='ironing'):
                        continue
                    entry = sources[kind].get(source_key)
                    if entry and not absent(entry['value']): explicit.append(dict(entry, kind=kind))
                    else:
                        if entry: nil.append(dict(entry, kind=kind))
                        if source_key in defaults['defaults'][kind]:
                            engine.append({'key': source_key, 'kind': kind, 'value': defaults['defaults'][kind][source_key]})
            if inputs and explicit: origin = 'explicit_source' if len(explicit)==1 else 'multiple_explicit_sources_review_precedence'
            elif inputs and engine: origin = 'orca_engine_default'
            elif inputs and key in ('orca_perimeter_speed_compatibility', 'retract_before_perimeters', 'orca_fixed_prime_volume', 'orca_wipe_compatibility'):
                origin = 'importer_semantic_adapter'
            elif inputs and key in ('custom_parameters_printer', 'custom_parameters_filament', 'default_tool_print'):
                origin = 'importer_generated'
            elif inputs: origin = 'importer_generated_or_untraced'
            elif explicit or engine: origin = 'source_not_emitted_review_hardware_or_unsupported'
            elif value == snap['ps3_defaults_for_hardware'].get(key): origin = 'ps3_only_default'
            else: origin = 'ps3_derived_or_untraced'
            fields[key] = {'origin':origin, 'effective':value, 'ps3_default':snap['ps3_defaults_for_hardware'].get(key),
                           'emitted_inputs':inputs, 'explicit_sources':explicit, 'orca_engine_defaults':engine, 'nil_sources':nil}
        unsupported = {}
        for kind in ('machine','process','filament'):
            unsupported[kind] = {k:v for k,v in defaults['defaults'][kind].items() if aliases.get(k,k) not in fields}
        unmapped = unmapped_source_fields(sources, defaults['defaults'], fields, aliases)
        report['snapshots'][name] = {'fields':fields,'engine_fields_without_direct_ps3_key':unsupported,
                                   'source_fields_without_direct_ps3_key': unmapped}
        counts = Counter(v['origin'] for v in fields.values())
        md += [f'## {name}', '', ', '.join(f'{k}: {v}' for k,v in sorted(counts.items())), '',
               '| PS3 field | Effective | Origin | Explicit source / Orca default |', '|---|---|---|---|']
        for key,row in fields.items():
            evidence = ['explicit '+x['key']+'='+compact(x['value']) for x in row['explicit_sources']]
            evidence += ['default '+x['key']+'='+compact(x['value']) for x in row['orca_engine_defaults']]
            md.append(f"| {key} | {compact(row['effective'])} | {row['origin']} | {'; '.join(evidence)} |")
        md += ['', '### Source fields without a direct PS3 key', '',
               'Review candidates, not a declaration that every field is unsupported. '
               'Metadata, custom G-code parameters and semantic adapters can also appear here. '
               'Explicit-only fields are included even when the engine snapshot omits them.', '',
               '| Source kind | Field | Value | Origin | Source profile |',
               '|---|---|---|---|---|']
        for kind, rows in unmapped.items():
            for key, row in rows.items():
                evidence = row['explicit_source'] or row['nil_source'] or {}
                md.append(f"| {kind} | {key} | {compact(row['value'])} | {row['origin']} | "
                          f"{compact(evidence.get('profile', ''))} |")
    args.output.write_text(json.dumps(report, indent=2))
    args.output.with_suffix('.md').write_text('\n'.join(md)+'\n', encoding='utf-8')
    print('Audit written to', args.output)


if __name__ == '__main__':
    main()
