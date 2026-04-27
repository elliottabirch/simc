#!/bin/bash
# Export APL JSON with a fresh APL extracted from C++ source.
# Uses the repo's MID1 profiles for character/gear, overlays the extracted APL.
#
# Usage: ./export-apl.sh <class.cpp> <output.json> --spec <spec> [--profile <override.simc>]
#
# Examples:
#   ./export-apl.sh engine/class_modules/apl/mage.cpp fire_apl_test.json --spec fire
#   ./export-apl.sh engine/class_modules/apl/mage.cpp arcane_apl_test.json --spec arcane
#   ./export-apl.sh engine/class_modules/apl/mage.cpp fire_apl_test.json --spec fire --profile my_fire_bis.simc

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SIMC="$SCRIPT_DIR/build/simc"
PROFILE_DIR="$SCRIPT_DIR/profiles/MID1"

usage() {
    echo "Usage: $0 <class.cpp> <output.json> --spec <spec> [--profile <override.simc>]"
    echo ""
    echo "Options:"
    echo "  --spec <spec>       Spec to extract (required, e.g., fire, arcane, frost)"
    echo "  --profile <file>    Override the base character profile (default: auto-detect from MID1)"
    echo ""
    echo "Examples:"
    echo "  $0 engine/class_modules/apl/mage.cpp fire_apl_test.json --spec fire"
    echo "  $0 engine/class_modules/apl/mage.cpp fire_apl_test.json --spec fire --profile fire_mage_bis.simc"
    exit 1
}

# Parse args
CPP_FILE=""
OUTPUT_JSON=""
SPEC=""
PROFILE_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --spec) SPEC="$2"; shift 2 ;;
        --profile) PROFILE_OVERRIDE="$2"; shift 2 ;;
        --help|-h) usage ;;
        *)
            if [[ -z "$CPP_FILE" ]]; then
                CPP_FILE="$1"
            elif [[ -z "$OUTPUT_JSON" ]]; then
                OUTPUT_JSON="$1"
            else
                echo "ERROR: Unexpected argument: $1"
                usage
            fi
            shift ;;
    esac
done

if [[ -z "$CPP_FILE" || -z "$OUTPUT_JSON" || -z "$SPEC" ]]; then
    usage
fi

if [[ ! -f "$CPP_FILE" ]]; then
    echo "ERROR: C++ source not found: $CPP_FILE"
    exit 1
fi

if [[ ! -f "$SIMC" ]]; then
    echo "ERROR: simc binary not found. Run ./build.sh first."
    exit 1
fi

# Derive class/spec title-case names (e.g., mage.cpp -> Mage, fire -> Fire)
# Strip common prefixes: sc_shaman.cpp -> shaman, apl_hunter.cpp -> hunter
CLASS_RAW="$(basename "$CPP_FILE" .cpp | sed 's/^sc_//;s/^apl_//')"
# Title-case every underscore-separated segment (beast_mastery -> Beast_Mastery)
title_case() { echo "$1" | awk -F_ '{for(i=1;i<=NF;i++) $i=toupper(substr($i,1,1)) tolower(substr($i,2))} 1' OFS=_; }
CLASS_TITLE="$(title_case "$CLASS_RAW")"
SPEC_TITLE="$(title_case "$SPEC")"

# Resolve the base character profile with fallback chain:
#   1. --profile override
#   2. profiles/MID1/MID1_<Class>_<Spec>.simc
#   3. profiles/PreRaids/PR_<Class>_<Spec>.simc
#   4. Generate from profiles/generators/MID1/MID1_Generate_<Class>.simc
if [[ -n "$PROFILE_OVERRIDE" ]]; then
    BASE_PROFILE="$PROFILE_OVERRIDE"
    if [[ ! -f "$BASE_PROFILE" ]]; then
        echo "ERROR: Specified profile not found: $BASE_PROFILE"
        exit 1
    fi
else
    MID1_PROFILE="$SCRIPT_DIR/profiles/MID1/MID1_${CLASS_TITLE}_${SPEC_TITLE}.simc"
    PR_PROFILE="$SCRIPT_DIR/profiles/PreRaids/PR_${CLASS_TITLE}_${SPEC_TITLE}.simc"
    GENERATOR="$SCRIPT_DIR/profiles/generators/MID1/MID1_Generate_${CLASS_TITLE}.simc"

    if [[ -f "$MID1_PROFILE" ]]; then
        BASE_PROFILE="$MID1_PROFILE"
    elif [[ -f "$PR_PROFILE" ]]; then
        echo "    (MID1 profile not found, falling back to PreRaids)"
        BASE_PROFILE="$PR_PROFILE"
    elif [[ -f "$GENERATOR" ]]; then
        echo "    (No MID1/PreRaids profile found, generating from $GENERATOR)"
        "$SIMC" "$GENERATOR"
        # Generator uses save= to write profiles into the profiles/ dir
        # Check both MID1 and current dir for the generated file
        if [[ -f "$MID1_PROFILE" ]]; then
            BASE_PROFILE="$MID1_PROFILE"
        else
            echo "ERROR: Generator ran but profile not found at: $MID1_PROFILE"
            echo "Generated files:"
            ls "$SCRIPT_DIR/profiles/MID1/"*"${CLASS_TITLE}"* 2>/dev/null | sed 's/^/  /'
            exit 1
        fi
    else
        echo "ERROR: No profile found for ${CLASS_TITLE} ${SPEC_TITLE}"
        echo "Searched:"
        echo "  $MID1_PROFILE"
        echo "  $PR_PROFILE"
        echo "  $GENERATOR"
        echo ""
        echo "Use --profile to specify a custom profile."
        exit 1
    fi
fi

# Extract .simc APL from C++ source into a temp directory
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

echo "==> Extracting APL from $CPP_FILE (spec: $SPEC)..."
python3 "$SCRIPT_DIR/scripts/cpp_to_simc.py" "$CPP_FILE" -o "$TMPDIR" --spec "$SPEC"

# Find the generated .simc file
APL_FILE="$(ls -t "$TMPDIR"/*.simc 2>/dev/null | head -1)"

if [[ -z "$APL_FILE" || ! -f "$APL_FILE" ]]; then
    echo "ERROR: No .simc file generated"
    exit 1
fi

# Copy the extracted APL as a sibling to the output JSON
OUTPUT_DIR="$(dirname "$OUTPUT_JSON")"
if [[ "$OUTPUT_DIR" == "." ]]; then
    OUTPUT_DIR="$PWD"
fi
mkdir -p "$OUTPUT_DIR"
cp "$APL_FILE" "$OUTPUT_DIR/"
APL_COPY="$OUTPUT_DIR/$(basename "$APL_FILE")"

echo ""
echo "==> Exporting APL JSON..."
echo "    Base profile: $BASE_PROFILE"
echo "    APL overlay:  $APL_COPY"
# Feed simc the base profile (character/gear) then the extracted APL (actions override)
"$SIMC" "$BASE_PROFILE" "$APL_COPY" apl_json="$OUTPUT_JSON"

echo ""
echo "Profile: $BASE_PROFILE"
echo "APL:     $APL_COPY"
echo "JSON:    $OUTPUT_JSON"
