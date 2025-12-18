# Quick Git Setup - Your Fork is Ready! 🚀

Your fork: **https://github.com/kanhaiya-gupta/PrusaSlicer.git**

## Run These Commands (Copy-Paste Ready)

```bash
# 1. Rename origin to upstream (original repo)
git remote rename origin upstream

# 2. Add your fork as origin
git remote add origin https://github.com/kanhaiya-gupta/PrusaSlicer.git

# 3. Verify remotes are correct
git remote -v
# Should show:
# origin    https://github.com/kanhaiya-gupta/PrusaSlicer.git (fetch)
# origin    https://github.com/kanhaiya-gupta/PrusaSlicer.git (push)
# upstream  https://github.com/prusa3d/PrusaSlicer.git (fetch)
# upstream  https://github.com/prusa3d/PrusaSlicer.git (push)

# 4. Create feature branch
git checkout -b feature/fiber-printing

# 5. Stage your changes
git add .gitignore
git add "doc/How to build - Linux et al.md"
git add doc/fiber/

# 6. Commit
git commit -m "Add fiber printing integration documentation and planning

- Add comprehensive documentation in doc/fiber/
- Update .gitignore for build files
- Update Linux build instructions with libtool requirement
- Add Mermaid diagrams for architecture visualization
- Add implementation roadmap and planning documents
- Add executive README and Git workflow guides"

# 7. Push to your fork
git push -u origin feature/fiber-printing
```

## After Setup

Your repository structure:
- **origin** → Your fork (kanhaiya-gupta/PrusaSlicer)
- **upstream** → Original (prusa3d/PrusaSlicer)
- **feature/fiber-printing** → Your work branch

## Daily Workflow

```bash
# Always work on feature branch
git checkout feature/fiber-printing

# Make changes, then:
git add .
git commit -m "Your commit message"
git push origin feature/fiber-printing

# To sync with original repository:
git fetch upstream
git merge upstream/master
```

---

**You're all set!** Your work is now safely in your own fork. 🎉

