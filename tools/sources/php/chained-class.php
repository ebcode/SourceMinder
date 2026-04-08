<?php

class Hello {

 public static function hey(){
  echo "hey\n";
  return __CLASS__;
 }
 public static function ho(){
   echo "ho\n";
   return __CLASS__;
 }
}

$h = new Hello();

$h::hey()::ho();
