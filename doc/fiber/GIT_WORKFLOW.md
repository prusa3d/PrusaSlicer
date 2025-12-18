# Git Workflow for Fiber Printing Integration

## ⚠️ Important: Don't Disturb the Original Repository

Since you're working on a fork/extension of PrusaSlicer, you should **never commit directly to the original repository's master branch**. Here's the recommended workflow:

---

## Recommended Approach: Feature Branch

### Option 1: Create a Feature Branch (Recommended)

This keeps your work separate and allows you to easily sync with upstream changes.

```bash
# 1. Create a new branch for fiber printing work
git checkout -b feature/fiber-printing

# 2. Stage your changes
git add .gitignore
git add "doc/How to build - Linux et al.md"
git add doc/fiber/

# 3. Commit your changes
git commit -m "Add fiber printing integration documentation and planning"

# 4. Push to your fork (if you have a remote)
git push -u origin feature/fiber-printing
```

**Benefits:**
- ✅ Keeps master branch clean
- ✅ Easy to switch between branches
- ✅ Can create pull requests later
- ✅ Easy to sync with upstream changes

---

## Option 2: Fork the Repository (Best for Long-term)

If you haven't already, fork PrusaSlicer to your own GitHub account:

1. **Fork on GitHub**: Go to PrusaSlicer repository → Click "Fork"
2. **Add your fork as remote**:
   ```bash
   # Check current remotes
   git remote -v
   
   # If you only have 'origin' pointing to original repo, add your fork
   git remote add myfork https://github.com/YOUR_USERNAME/PrusaSlicer.git
   
   # Or rename origin to upstream and add your fork as origin
   git remote rename origin upstream
   git remote add origin https://github.com/YOUR_USERNAME/PrusaSlicer.git
   ```

3. **Work on feature branch**:
   ```bash
   git checkout -b feature/fiber-printing
   # Make your changes
   git add .
   git commit -m "Your commit message"
   git push origin feature/fiber-printing
   ```

**Benefits:**
- ✅ Complete separation from original repo
- ✅ Your own repository to manage
- ✅ Can create pull requests to original (if desired)
- ✅ Full control over your work

---

## Current Situation: What to Do Now

You're currently on `master` with uncommitted changes. Here's what to do:

### Step 1: Create Feature Branch

```bash
# Create and switch to new branch
git checkout -b feature/fiber-printing
```

### Step 2: Commit Your Changes

```bash
# Stage all your fiber-related changes
git add .gitignore
git add "doc/How to build - Linux et al.md"
git add doc/fiber/

# Commit
git commit -m "Add fiber printing integration documentation

- Add comprehensive documentation in doc/fiber/
- Update .gitignore for build files
- Update Linux build instructions with libtool requirement
- Add Mermaid diagrams for architecture visualization
- Add implementation roadmap and planning documents"
```

### Step 3: Push to Your Repository

```bash
# If you have your own fork/remote
git push -u origin feature/fiber-printing

# Or if you need to set up remote first
git remote add myfork <your-fork-url>
git push -u myfork feature/fiber-printing
```

### Step 4: Keep Master Clean (Optional)

If you want to keep master branch clean and synced with upstream:

```bash
# Switch back to master
git checkout master

# Discard any uncommitted changes on master (if any)
git restore .

# Or reset master to match upstream (if you have upstream remote)
git fetch upstream
git reset --hard upstream/master
```

---

## Recommended Branch Structure

```
master (original/upstream)
  └── feature/fiber-printing (your work)
       ├── doc/fiber/ (documentation)
       ├── src/libslic3r/Fiber/ (future code)
       └── ... (other changes)
```

---

## Daily Workflow

### Starting Work
```bash
# Switch to your feature branch
git checkout feature/fiber-printing

# Pull latest changes from upstream (if needed)
git fetch upstream
git merge upstream/master  # or rebase if preferred
```

### Making Changes
```bash
# Make your changes
# ... edit files ...

# Stage and commit
git add .
git commit -m "Descriptive commit message"

# Push to your fork
git push origin feature/fiber-printing
```

### Syncing with Upstream
```bash
# Fetch latest from original repository
git fetch upstream

# Merge upstream changes into your branch
git merge upstream/master

# Or rebase (cleaner history)
git rebase upstream/master
```

---

## Branch Naming Conventions

Good branch names:
- `feature/fiber-printing` - Main feature branch
- `feature/fiber-path-planning` - Specific feature
- `feature/fiber-gui` - GUI components
- `bugfix/fiber-gcode-issue` - Bug fixes
- `docs/fiber-documentation` - Documentation only

---

## Protecting the Original Repository

### Never Do This:
```bash
# ❌ DON'T commit directly to master
git checkout master
git add .
git commit -m "..."  # BAD!

# ❌ DON'T force push to original repo
git push --force origin master  # VERY BAD!
```

### Always Do This:
```bash
# ✅ Work on feature branch
git checkout -b feature/fiber-printing

# ✅ Commit to your branch
git commit -m "..."

# ✅ Push to your fork
git push origin feature/fiber-printing
```

---

## Setting Up Remotes (If Not Done)

```bash
# Check current remotes
git remote -v

# If you only have 'origin' pointing to original PrusaSlicer:
# Option A: Rename and add your fork
git remote rename origin upstream
git remote add origin https://github.com/YOUR_USERNAME/PrusaSlicer.git

# Option B: Keep origin, add your fork as separate remote
git remote add myfork https://github.com/YOUR_USERNAME/PrusaSlicer.git
```

---

## Quick Reference Commands

```bash
# Create feature branch
git checkout -b feature/fiber-printing

# See what branch you're on
git branch

# See all branches
git branch -a

# Switch branches
git checkout feature/fiber-printing

# Commit changes
git add .
git commit -m "Your message"

# Push to your fork
git push origin feature/fiber-printing

# Sync with upstream
git fetch upstream
git merge upstream/master
```

---

## Summary

**Best Practice:**
1. ✅ **Fork the repository** to your GitHub account
2. ✅ **Create feature branch** (`feature/fiber-printing`)
3. ✅ **Work on feature branch** - never on master
4. ✅ **Push to your fork** - not to original repo
5. ✅ **Sync with upstream** regularly to get updates

This way:
- ✅ Original repository stays untouched
- ✅ Your work is organized and safe
- ✅ Easy to collaborate and share
- ✅ Can create PRs if needed
- ✅ Full control over your codebase

---

## Next Steps

1. **Create feature branch**:
   ```bash
   git checkout -b feature/fiber-printing
   ```

2. **Commit your current work**:
   ```bash
   git add doc/fiber/ .gitignore "doc/How to build - Linux et al.md"
   git commit -m "Initial fiber printing documentation and planning"
   ```

3. **Set up your fork** (if not done):
   - Fork PrusaSlicer on GitHub
   - Add your fork as remote
   - Push your branch

4. **Continue working** on the feature branch!

---

*Remember: Always work on a feature branch, never directly on master!* 🛡️

