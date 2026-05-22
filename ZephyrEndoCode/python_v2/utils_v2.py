import os
import datetime
import json
def dict_to_file(d, out_dir):
	with open(out_dir, 'w') as file:
		for key, value in sorted(d.items()):
			file.write("{} = {}\n".format(key, value))

def create_runs_folder(args):
	# file_name = "{}".format(args.run_name)
	folder_name = args.run_name + datetime.datetime.now().strftime('_%Y%m%d_%H-%M-%S')
	folder_name += "_{}".format(args.comment) if args.comment != "" else ""
	
	if args.debug:
		folder_name = 'debug'
	if not os.path.exists(args.data_dir):
		os.mkdir(args.data_dir)
	result_folder = os.path.join(args.data_dir, folder_name)
	# output_folder = os.path.join(result_folder, 'output')
	# vis_output_folder = os.path.join(result_folder,'vis')
	if not os.path.exists(result_folder):
		os.mkdir(result_folder)	
		# os.mkdir(output_folder)
		# os.mkdir(vis_output_folder)

	dict_to_file(vars(args), os.path.join(result_folder, 'args.txt'))
	with open(os.path.join(result_folder, 'args.json'), 'w') as file:
		json.dump(vars(args), file, indent=4)

	return result_folder

import queue
def qdumper(q):
    try:
        yield q.get(False)
    except queue.Empty:
        pass
