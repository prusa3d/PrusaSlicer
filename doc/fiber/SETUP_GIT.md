# Git Setup: Step-by-Step Guide

## ✅ Recommended Order: Fork First, Then Work

### Step 1: Fork the Repository on GitHub

1. Go to: https://github.com/prusa3d/PrusaSlicer
2. Click the **"Fork"** button (top right)
3. This creates your own copy: `https://github.com/YOUR_USERNAME/PrusaSlicer`

**Why fork first?**
- ✅ You get your own repository on GitHub
- ✅ You can push/pull without affecting original
- ✅ Clean separation from the start
- ✅ Easy to set up remotes correctly

---

### Step 2: Set Up Your Local Repository

Since you already have the repository cloned, you have two options:

#### Option A: Add Your Fork as Remote (Recommended - Keeps Your Work)

```bash
# 1. Check current remotes
git remote -v

# 2. Rename origin to upstream (points to original)
git remote rename origin upstream

# 3. Add your fork as origin
git remote add origin https://github.com/YOUR_USERNAME/PrusaSlicer.git

# 4. Verify
git remote -v
# Should show:
# origin    https://github.com/YOUR_USERNAME/PrusaSlicer.git (your fork)
# upstream  https://github.com/prusa3d/PrusaSlicer.git (original)
```

#### Option B: Fresh Clone from Your Fork (Clean Start)

```bash
# 1. Backup your current work
cd ..
cp -r PrusaSlicer PrusaSlicer_backup

# 2. Clone your fork
git clone https://github.com/YOUR_USERNAME/PrusaSlicer.git

# 3. Add upstream remote
cd PrusaSlicer
git remote add upstream https://github.com/prusa3d/PrusaSlicer.git

# 4. Copy your work from backup
cp -r ../PrusaSlicer_backup/doc/fiber doc/
cp ../PrusaSlicer_backup/.gitignore .
# etc.
```

**I recommend Option A** - it's simpler and keeps your current work.

---

### Step 3: Create Feature Branch

```bash
# Create and switch to feature branch
git checkout -b feature/fiber-printing

# Verify you're on the new branch
git branch
# Should show * feature/fiber-printing
```

---

### Step 4: Commit Your Current Work

```bash
# Stage your changes
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

---

### Step 5: Push to Your Fork

```bash
# Push your feature branch to your fork
git push -u origin feature/fiber-printing
```

---

## Complete Setup Commands (Copy-Paste Ready)

Replace `YOUR_USERNAME` with your GitHub username:

```bash
# 1. Set up remotes
git remote rename origin upstream
git remote add origin https://github.com/YOUR_USERNAME/PrusaSlicer.git

# 2. Create feature branch
git checkout -b feature/fiber-printing

# 3. Commit your work
git add .gitignore "doc/How to build - Linux et al.md" doc/fiber/
git commit -m "Add fiber printing integration documentation and planning"

# 4. Push to your fork
git push -u origin feature/fiber-printing
```

---

## Verify Your Setup

```bash
# Check remotes
git remote -v
# Should show:
# origin    https://github.com/YOUR_USERNAME/PrusaSlicer.git (fetch)
# origin    https://github.com/YOUR_USERNAME/PrusaSlicer.git (push)
# upstream  https://github.com/prusa3d/PrusaSlicer.git (fetch)
# upstream  https://github.com/prusa3d/PrusaSlicer.git (push)

# Check current branch
git branch
# Should show: * feature/fiber-printing

# Check status
git status
# Should show: "Your branch is up to date with 'origin/feature/fiber-printing'"
```

---

## Daily Workflow (After Setup)

```bash
# Always work on feature branch
git checkout feature/fiber-printing

# Make changes, then:
git add .
git commit -m "Your commit message"
git push origin feature/fiber-printing

# To sync with original repository updates:
git fetch upstream
git merge upstream/master
```

---

## Summary

**Yes, fork first!** Here's why:

1. ✅ **Clean separation** - Your own repository
2. ✅ **Safe to push** - Won't affect original
3. ✅ **Easy setup** - Just add remotes
4. ✅ **Professional** - Standard open-source workflow
5. ✅ **Future-proof** - Easy to create PRs if needed

**Order:**
1. Fork on GitHub → 2. Set up remotes → 3. Create branch → 4. Commit → 5. Push

---

## Troubleshooting

### "Remote origin already exists"
```bash
# Remove existing origin
git remote remove origin

# Add your fork
git remote add origin https://github.com/YOUR_USERNAME/PrusaSlicer.git
```

### "Cannot rename remote 'origin'"
```bash
# Check what remotes exist
git remote -v

# If origin points to your fork already, you're good!
# If it points to original, rename it:
git remote set-url origin https://github.com/YOUR_USERNAME/PrusaSlicer.git
git remote add upstream https://github.com/prusa3d/PrusaSlicer.git
```

---

*After forking, follow the setup commands above and you'll be ready to work safely!* 🚀

