set terminal png
set output 'perf.png'

set xlabel 'number of fibonacci sequences'
set ylabel 'times (ns)
plot 'dbl.out' using 1:2 title "Fast doubling" with linespoints, \
     'dp.out'  using 1:2 title "Dynamic Programming" with linespoints
