#!/usr/bin/env python3
import json
import copy
import sys

def replace_ids(obj, old_prefix, new_prefix):
    """Recursively replace objID and identifier prefixes."""
    if isinstance(obj, dict):
        new_obj = {}
        for key, value in obj.items():
            if key == 'objID' and isinstance(value, str):
                # Replace prefix in objID
                if value.startswith(old_prefix):
                    value = new_prefix + value[len(old_prefix):]
                # Also handle style IDs
                elif '-style-' in value and old_prefix in value:
                    value = value.replace(old_prefix, new_prefix)
            elif key == 'identifier' and isinstance(value, str):
                # Replace identifier prefix
                if value.startswith(old_prefix):
                    value = new_prefix + value[len(old_prefix):]
            elif key == 'style' and isinstance(value, dict) and 'objID' in value:
                # Replace style objID
                style_id = value['objID']
                if style_id.startswith(old_prefix):
                    value['objID'] = new_prefix + style_id[len(old_prefix):]
            # Recurse
            new_obj[key] = replace_ids(value, old_prefix, new_prefix)
        return new_obj
    elif isinstance(obj, list):
        return [replace_ids(item, old_prefix, new_prefix) for item in obj]
    else:
        return obj

def main():
    filename = "KitchenHelp.eez-project"
    with open(filename, 'r', encoding='utf-8') as f:
        data = json.load(f)

    screens = data['userPages']
    if len(screens) != 2:
        print(f"Expected 2 screens, found {len(screens)}")
        sys.exit(1)

    # Second screen is recipe detail
    detail_screen = screens[1]
    # Deep copy
    phase_screen = copy.deepcopy(detail_screen)

    # Update top-level properties
    phase_screen['objID'] = 'recipe-phase-screen-0000-0000-0000-000000000001'
    phase_screen['name'] = 'Recipe Phase'

    # Replace prefixes in the entire screen object
    # objID prefix: recipe-detail -> recipe-phase
    # identifier prefix: recipe_ -> phase_
    # But keep screen identifier as recipe_phase_screen (we'll handle separately)
    # First replace objIDs and style IDs
    phase_screen = replace_ids(phase_screen, 'recipe-detail', 'recipe-phase')
    # Now replace identifiers (recipe_ -> phase_), but exclude screen identifier
    # We'll do a targeted replacement: only identifiers that start with recipe_
    # We'll keep the screen identifier as recipe_phase_screen.
    # Let's define a function to replace identifiers recursively, skipping the screen widget.
    def replace_identifiers(obj, screen_widget_seen=False):
        if isinstance(obj, dict):
            new_dict = {}
            for k, v in obj.items():
                if k == 'identifier' and isinstance(v, str):
                    # Screen widget identifier should become recipe_phase_screen
                    if v == 'recipe_detail_screen':
                        v = 'recipe_phase_screen'
                    elif v.startswith('recipe_'):
                        v = 'phase_' + v[7:]  # remove 'recipe_'
                new_dict[k] = replace_identifiers(v, screen_widget_seen or k == 'identifier')
            return new_dict
        elif isinstance(obj, list):
            return [replace_identifiers(item, screen_widget_seen) for item in obj]
        else:
            return obj

    phase_screen = replace_identifiers(phase_screen)

    # Insert as third screen
    screens.append(phase_screen)

    # Write back with same indentation
    with open(filename, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)

    print("Added new recipe phase screen as third screen.")
    print("Please verify the file is valid JSON.")

if __name__ == '__main__':
    main()