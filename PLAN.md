our goal is to replace PlotJuggler by merging the functionality we use into cabana
* there's a copy of PlotJuggler in .context/
* we want to match PlotJuggler's functionality that is used in the layouts/ folder: rlog parsing, plotting, custom functions
* let's use Qwt for plotting like PJ
* i want to be able to open my sotred layouts, though you can port them to a differnt format if you like
* both are Qt apps, so we should start by directly porting the PJ code into cabana, then trimming it down to what we need and making it match our repo style
* for the ui, we should just have a PJ mode in cabana now that replace two of the three main columns in the ui with the message list from plotjuggler and the plot area. i want to keep the third column from cabana for the video
  * most of PJ's space is taken up by plots, which is import for the PJ mode. that whole middle area should be plots in our new version
  * we need drag & drop


validation:
* builds
* runs without crashing
* use screenshots to ensure its visually good
