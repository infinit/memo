(c++-project
 '(
   "src"
   "elle/src"
   )
 "_build/linux64"
 (concat "./drake -j " (int-to-string system-cores-logical) " //build")
 ""
 '()
)
