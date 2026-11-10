# 🤝 Team Collaboration Guide — SimpleRISC Simulator

This guide outlines how to set up your team workspace and collaborate across different laptops.

---

## 1. 👥 Adding Friends to the GitHub Repository

1. Go to your repository on GitHub: `https://github.com/omkirannariki2008/SimpleRisc201`.
2. Click **Settings** (tab at the top right of the repo).
3. In the left sidebar, click **Collaborators** (under *Access*).
4. Click the green **Add people** button.
5. Search for your friends' GitHub usernames or email addresses and send them invites.
6. Once they accept the email invitation (or notification on GitHub), they will have full push/pull access!

---

## 2. 💻 Getting Started on a Friend's Laptop

On each friend's laptop:

### Step 1: Clone the Repository
Open a terminal (PowerShell, Command Prompt, or bash) and run:
```bash
git clone https://github.com/omkirannariki2008/SimpleRisc201.git
cd SimpleRisc201
```

### Step 2: Open in VS Code
```bash
code .
```

### Step 3: Prerequisites
Make sure **g++** (MinGW-w64 on Windows, or GCC/Clang on Linux/macOS) is installed:
```bash
g++ --version
```

### Step 4: Build & Run
- **Shortcut in VS Code:** Press `Ctrl+Shift+B` (Windows/Linux) or `Cmd+Shift+B` (Mac) to build.
- **Manual Build:**
  ```bash
  g++ -std=c++14 -O2 -I include src/alu.cpp src/vertical_control_unit.cpp src/horizontal_control_unit.cpp src/assembler.cpp src/main.cpp -o eval1_demo.exe
  ./eval1_demo.exe
  ```

---

## 3. 🌿 Recommended Git Workflow for Working Together

To avoid code conflicts when multiple people are coding at the same time:

### Rule 1: Always Pull Before Starting Work
Before you write any new code, pull the latest changes from your teammates:
```bash
git checkout main
git pull
```

### Rule 2: Work in Feature Branches
Never commit directly to `main` at the same time as someone else. Create a branch for your task:
```bash
# Example for person working on memory or pipelining
git checkout -b feature/memory-subsystem

# Make your changes, then commit:
git add .
git commit -m "Implement data memory read/write cycles"

# Push your branch to GitHub:
git push -u origin feature/memory-subsystem
```

### Rule 3: Merging Your Code
1. Go to GitHub and click **Compare & pull request**.
2. Have your teammate quickly review it and click **Merge pull request**.
3. Teammates can then run `git checkout main && git pull` to receive the updates!

---

## 4. ⚡ Real-Time Co-Coding (Live Share)

If you and your friends want to code together in real-time on a voice call:
1. Install the **Live Share** extension in VS Code.
2. Click **Live Share** in the bottom status bar to generate an invite link.
3. Send the link to your friend. They can join directly in their VS Code to edit files and debug together synchronously!
