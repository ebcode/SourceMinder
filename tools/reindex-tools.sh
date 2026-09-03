#!/bin/bash
for x in {c,go,javascript,perl,php,python,ruby,rust,typescript}
do
  echo "cd to sources/$x"
  cd "sources/$x"
  rm code-index.db
  if  [ "$x" = "typescript" -o "$x" = "javascript" ]
  then
    echo "change $x to ts"
    x='ts'
  fi

  echo "run index-$x"
  index-$x . --once
  cd ../..
done
