#!/usr/bin/env python3
import re
import sys

def main():
    filename = "KitchenHelp.eez-project"
    with open(filename, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    # Line numbers are 1-indexed (from grep output)
    start_line = 2778  # line where second element starts (including leading spaces)
    end_line = 4240    # line where second element ends (closing brace)
    # Convert to 0-indexed
    start_idx = start_line - 1
    end_idx = end_line - 1

    block = lines[start_idx:end_idx+1]  # inclusive

    # Now modify the block lines
    new_block = []
    for line in block:
        # Replace objIDs: recipe-detail-obj-xxxx -> recipe-phase-obj-xxxx
        line = re.sub(r'recipe-detail-obj-(\d+)', r'recipe-phase-obj-\1', line)
        # Replace style IDs: recipe-detail-style-xxxx -> recipe-phase-style-xxxx
        line = re.sub(r'recipe-detail-style-(\d+)', r'recipe-phase-style-\1', line)
        # Replace screen objID: recipe-detail-screen-... -> recipe-phase-screen-...
        line = re.sub(r'recipe-detail-screen-([0-9a-f-]+)', r'recipe-phase-screen-\1', line)
        # Replace identifier recipe_detail_screen -> recipe_phase_screen
        line = line.replace('recipe_detail_screen', 'recipe_phase_screen')
        # Replace other identifiers: recipe_ -> phase_ (for widgets)
        # But careful not to replace in middle of words. Use regex word boundaries.
        # We'll replace only known identifiers: recipe_back_btn, recipe_title, recipe_favourite_add, etc.
        # Let's do a more generic replacement: recipe_ -> phase_
        # This will also affect recipe_detail_screen (already replaced) and recipe_phase_screen (should not affect)
        # We'll do after previous replacement.
        line = re.sub(r'\brecipe_', 'phase_', line)
        # Replace name "Recipe Detail" maybe not present. We'll change name field.
        # We'll handle separately by checking if line contains '"name":'
        if '"name":' in line:
            # The name is likely "Recipe Detail". Change to "Recipe Phase"
            line = re.sub(r'"Recipe Detail"', '"Recipe Phase"', line)
        # Also change the screen's name property (outside components). We'll handle later.
        new_block.append(line)

    # Now we need to insert new block after the old block, with a comma after old block.
    # Currently lines[end_idx] is the closing brace of second element, no comma.
    # Add comma to that line in original lines.
    lines[end_idx] = lines[end_idx].rstrip() + ',\n'
    # Insert new block after lines[end_idx] (i.e., at position end_idx+1)
    # We'll need to adjust line numbers after insertion, but we'll just construct new lines list.
    new_lines = lines[:end_idx+1] + new_block + lines[end_idx+1:]

    # Write to new file
    out_filename = "KitchenHelp.eez-project.new"
    with open(out_filename, 'w', encoding='utf-8') as f:
        f.writelines(new_lines)

    print(f"Modified file written to {out_filename}")
    print(f"Original backed up as KitchenHelp.eez-project.backup")

if __name__ == '__main__':
    main()