"""Attach source evidence to the native/Orca comparison; never infer Orca defaults from native Prusa."""
import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


def profile_index(root):
    manifest = json.loads((root / 'Prusa.json').read_text(encoding='utf-8-sig'))
    index = {}
    for group in ('machine_list', 'process_list', 'filament_list'):
        for entry in manifest.get(group, []):
            path = root / 'Prusa' / entry['sub_path']
            data = json.loads(path.read_text(encoding='utf-8-sig'))
            index[(data['type'], data['name'])] = (data, path)
    return index


def flatten(index, kind, name, active=()):
    identity = (kind, name)
    if identity in active:
        raise ValueError(f'Inheritance cycle: {identity}')
    data, path = index[identity]
    result = flatten(index, kind, data['inherits'], (*active, identity)) if data.get('inherits') else {}
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    for key, value in data.items():
        result[key] = {'key': key, 'value': value, 'file': str(path.resolve()),
                       'profile': name, 'sha256': digest}
    return result


def absent(value):
    return value is None or value == 'nil' or (isinstance(value, list) and bool(value) and all(absent(v) for v in value))


def compact(value):
    if isinstance(value, list) and value and all(v == value[0] for v in value):
        return f'{compact(value[0])} (x{len(value)})'
    if isinstance(value, str) and ('\n' in value or len(value) > 130):
        return f'text: {len(value)} characters; SHA256 {hashlib.sha256(value.encode()).hexdigest()[:12]}'
    return json.dumps(value, ensure_ascii=False).replace('|', '\\|')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--orca-profiles', type=Path, required=True)
    parser.add_argument('--mappings', type=Path, default=Path(__file__).resolve().parents[2] / 'src/slic3r-shared/src/Slic3r/Biz/Preset/IO/OrcaProfileMappings.inc')
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    mappings_text = args.mappings.read_text().split('static const std::set')[0]
    aliases = dict(re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', mappings_text))
    aliases.update(support_style='support_material_style', support_type='support_material_style')
    index = profile_index(args.orca_profiles)
    evidence = {}
    markdown = ['# Native / imported Prusa comparison', '',
        'Generated from real evaluated 0.20 mm SPEED / 0.4 mm / Prusament PLA presets.', '',
        'Origins below refer to the imported side. An explicit inherited source value is still explicit. '
        'PS3 fallback values are not evidence of Orca engine defaults. Multiple candidates and untraced derivations '
        'remain visible; this report does not label every difference correct or incorrect.', '',
        'The JSON evidence companion includes every effective field, raw source values, source file hashes, '
        'emitted converter inputs, and PS3 defaults for the same hardware. Script text hashes indicate differences, '
        'not behavioral equivalence. Numeric comparisons here are exact.', '']
    for name, pair in report.items():
        imported = pair['imported']
        sources = {kind: flatten(index, kind, selected) for kind, selected in (
            ('machine', imported['name']), ('process', imported['process_name']),
            ('filament', imported['filament_name']))}
        inputs = imported['converted_inputs']
        rows = {}
        for key, value in imported['effective'].items():
            emitted = {group: values[key] for group, values in inputs.items() if key in values}
            candidates = []
            ignored = []
            for kind, fields in sources.items():
                for source_key, entry in fields.items():
                    if aliases.get(source_key, source_key) != key:
                        continue
                    candidate = dict(entry, kind=kind)
                    (ignored if absent(entry['value']) else candidates).append(candidate)
            # This one-to-many translation is explicit in the converter, not a PS3 default.
            if key == 'first_layer_solid_infill_speed' and 'initial_layer_infill_speed' in sources['process']:
                candidates.append(dict(sources['process']['initial_layer_infill_speed'], kind='process',
                                       transformation='Orca uses one initial-layer infill speed for sparse and solid'))
            if key == 'default_material' and 'default_filament_profile' in sources['machine']:
                candidates.append(dict(sources['machine']['default_filament_profile'], kind='machine',
                                       transformation='Compatible default filament selection'))
            if emitted and candidates:
                origin = 'explicit_source' if len(candidates) == 1 else 'multiple_explicit_sources_review_precedence'
            elif emitted and key in ('preheat_time', 'preheat_steps', 'small_perimeter_threshold', 'enable_dynamic_overhang_speeds'):
                origin = 'importer_default'
            elif emitted and key in ('custom_parameters_printer', 'custom_parameters_filament', 'default_tool_print'):
                origin = 'importer_generated'
            elif emitted:
                origin = 'importer_generated_or_untraced'
            elif candidates and key in ('nozzle_diameter', 'printer_model'):
                origin = 'hardware_conversion'
            elif candidates:
                origin = 'source_present_but_not_emitted_review'
            elif value == imported['ps3_defaults_for_hardware'].get(key):
                origin = 'ps3_default'
            else:
                origin = 'ps3_derived_or_untraced'
            rows[key] = {'origin': origin, 'native': pair['native']['effective'].get(key), 'imported': value,
                         'ps3_default': imported['ps3_defaults_for_hardware'].get(key),
                         'emitted_inputs': emitted, 'source_candidates': candidates, 'ignored_nil_sources': ignored}
            if origin == 'importer_default':
                rows[key]['evidence'] = 'OrcaProfileConverter.cpp values() explicitly injects preheat_time=30 / preheat_steps=1 / small_perimeter_threshold=0 / enable_dynamic_overhang_speeds=true before applying source settings. These match the defaults in local Orca PrintConfig.cpp; no native Prusa comparison is used to infer them.'
        evidence[name] = {'source_profiles': sources, 'fields': rows}
        differences = {key: row for key, row in rows.items() if row['native'] != row['imported']}
        counts = Counter(row['origin'] for row in differences.values())
        markdown += [f'## {name}', '', f"Native process: `{pair['native']['process_name']}`; tool preset: `{pair['native']['tool_name']}`.",
                     f"Imported process: `{imported['process_name']}`.", '',
                     f'{len(differences)} exact differing fields out of {len(rows)}. Origins: ' + ', '.join(f'{k}: {v}' for k,v in sorted(counts.items())) + '.', '',
                     '| Field | Native | Imported | Imported origin | Source evidence |',
                     '|---|---|---|---|---|']
        for key, row in sorted(differences.items()):
            source = '; '.join(f"{Path(s['file']).name}: {s['key']} = {compact(s['value'])}" for s in row['source_candidates'])
            markdown.append(f"| `{key}` | {compact(row['native'])} | {compact(row['imported'])} | {row['origin']} | {source} |")
        markdown.append('')
    args.report.with_suffix('.sources.json').write_text(json.dumps(evidence, indent=2) + '\n')
    args.report.with_suffix('.md').write_text('\n'.join(markdown) + '\n', encoding='utf-8')
    print(f'Source evidence and Markdown written beside {args.report}')


if __name__ == '__main__':
    main()
