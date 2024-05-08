BOW-TRACKER
Dataset Collection Files - README
---------------------------------

- /dataset_collection_jig_design_files/ - the design files for making the grid used in the dataset collection jig
- /dataset_values_to_measure/
	- dataset_combinations.txt - the set of combinations of bow position and HSD which are loaded by the dataset collection Max patch
	- dataset_combinations.ipynb - the Jupyter Notebook used to create the above
- /dataset/ - the save location for .CSV datasets created using dataset_collector.maxpat
- dataset_collector.maxpat - the Max patch for collecting the dataset. It does the following:
	- Loads dataset_combinations.txt to determine which combinations to present the user
	- Receives proxy distance values from main.py
	- Stores combinations alongside distance values to create dataset examples, and can save these to a CSV file
- beep.wav - a sound effect used by dataset_collector.maxpat, so the user does not need to look at the screen
- dataset_parser.ipynb - a notebook which takes the CSV files output by dataset_collector.txt, and adds column headings etc. Only needs to be run once on each new dataset from the Max patch
- dataset_analysis.ipynb - this notebook contains code to make various plots of the dataset, including:
	- Number of dataset examples per combination of bow position and HSD
	- Distribution of sensor values around the mean for each combination
- dataset_nsd_rescaling.ipynb - this notebook is used to calculate force values from position and HSD. This creates the force dataset used to train the bow tracking model, given the inaccuracy in position
- README.txt - this file