## git config 

git config --global user.name "Mengbo Ren"
git config --global user.Email "a1837374@student.adelaide.edu.au"

## create a new repository on the command line

echo "# 3001-Computer-Networks-" >> README.md
git init
git add README.md
git commit -m "first commit"
git branch -M main
git remote add origin https://github.com/renMengbo/3001-Computer-Networks-.git
git push -u origin main

## push an existing repository from the command line

git remote add origin https://github.com/renMengbo/3001-Computer-Networks-.git
git branch -M main
git push -u origin main