set follow-fork-mode parent
file parallel
break 290
#break 504
#display command
#r --jobs 2 echo ::: a b c d
#r -j2 echo "Hello {2} {1}" ::: a b c ::: 1 2 3
#r -j4 ./fib ::: 100 50 10
r --dryrun echo '{#} {%} {} --> {.} && {/} && {/.} && {//}' ::: `ls ./*` 
