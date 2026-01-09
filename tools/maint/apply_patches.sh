#!/bin/bash

# Configuration
UPSTREAM_REMOTE="upstream"
BASE_BRANCH="main"        # Change to 'master' if that's what upstream uses
TARGET_BRANCH="main-integrated"
MANIFEST_FILE="tools/maint/patch_manifest.log"

# Function to reset the branch
reset_to_upstream() {
    echo "Warning: This will wipe all local changes on $TARGET_BRANCH."
    read -p "Are you sure you want to reset to $UPSTREAM_REMOTE/$BASE_BRANCH? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        git fetch "$UPSTREAM_REMOTE"
        git checkout -B "$TARGET_BRANCH" "$UPSTREAM_REMOTE/$BASE_BRANCH"
        echo "--- Reset to Upstream: $(date) ---" > "$MANIFEST_FILE"
        echo "Base Commit: $(git rev-parse HEAD)" >> "$MANIFEST_FILE"
        echo "Reset successful. Branch is now clean."
    else
        echo "Reset aborted."
    fi
}

# Check for reset flag
if [[ "$1" == "--reset" ]]; then
    reset_to_upstream
    exit 0
fi

# Standard PR application logic
PR_IDS=("$@")
if [ ${#PR_IDS[@]} -eq 0 ]; then
    echo "Usage:"
    echo "  Apply PRs: $0 <PR_ID1> <PR_ID2> ..."
    echo "  Reset:     $0 --reset"
    exit 1
fi

# Ensure target branch exists and is checked out
git checkout -B "$TARGET_BRANCH"

echo "--- Patch Session: $(date) ---" >> "$MANIFEST_FILE"

for PR in "${PR_IDS[@]}"; do
    echo "--------------------------------------"
    echo "Fetching PR #$PR..."
    
    if git fetch "$UPSTREAM_REMOTE" "pull/$PR/head:PR_TEMP_FETCH"; then
        if git merge PR_TEMP_FETCH --no-edit -m "Integrate upstream PR #$PR"; then
            echo "Successfully integrated PR #$PR"
            echo "PR #$PR SHA: $(git rev-parse PR_TEMP_FETCH)" >> "$MANIFEST_FILE"
        else
            echo "Conflict in PR #$PR. Aborting merge."
            git merge --abort
            exit 1
        fi
        git branch -D PR_TEMP_FETCH
    else
        echo "Error: PR #$PR not found."
    fi
done

echo "--------------------------------------"
echo "Done. See $MANIFEST_FILE for details."