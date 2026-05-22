import pickle
import numpy as np
import matplotlib.pyplot as plt
import os
import utils_aruco
import utils_icp
import copy
from matplotlib.ticker import FuncFormatter
import circle_fit as cf
from scipy.optimize import curve_fit, minimize
import cv2

def interp_extrapolate(x, xp, fp):
    """
    Interpolates with extrapolation for x values outside the range of xp.
    """
    y = np.interp(x, xp, fp)
    
    # Extrapolate for x values outside the range of xp
    mask_low = x < xp[0]
    mask_high = x > xp[-1]

    if np.any(mask_low):
        slope_low = (fp[1] - fp[0]) / (xp[1] - xp[0])
        y[mask_low] = fp[0] + slope_low * (x[mask_low] - xp[0])

    if np.any(mask_high):
        slope_high = (fp[-1] - fp[-2]) / (xp[-1] - xp[-2])
        y[mask_high] = fp[-1] + slope_high * (x[mask_high] - xp[-1])
    
    return y


def map_range(x, x_min1, x_max1, y_min2, y_max2):
    # Convert x to a NumPy array if it's not already, so the function can handle both scalars and arrays
    x = np.array(x, dtype=float)
    return (x - x_min1) * (y_max2 - y_min2) / (x_max1 - x_min1) + y_min2

def plot_circle_arc(ax, center, radius, start_angle, end_angle, resolution=100, flip_xy=False, flip_x=False):
    theta = np.linspace(start_angle, end_angle, resolution)
    x = center[0] + radius * np.cos(theta)
    y = center[1] + radius * np.sin(theta)
    if flip_xy:
        if flip_x:
            return ax.plot(-y, -x, label='fitted circle', color='black')
        else:
            return ax.plot(y, -x, label='fitted circle', color='black')
    elif flip_x:
        return ax.plot(-x, y, label='fitted circle', color='black')
    else:
        return ax.plot(x, y, label='fitted circle', color='black')


# Function to save the nth frame from a video as an image
def save_nth_frame(video_path, n, output_path, use_buffer):
    # Open the video file
    cap = cv2.VideoCapture(video_path)
    if use_buffer and os.path.exists(output_path):
        print(f"Image already exists at {output_path}. Skipping...")
        return

    # Check if the video file is opened successfully
    if not cap.isOpened():
        print("Error: Could not open video file.")
        return

    # Iterate through the video frames until the nth frame
    for _ in range(n):
        ret, frame = cap.read()
        if not ret:
            print(f"Error: Could not read frame {n} from the video.")
            cap.release()
            return

    # Save the nth frame as an image
    cv2.imwrite(output_path, frame)
    print(f"Frame {n} saved as {output_path}")

    # Release the capture object
    cap.release()

def angle_on_circle(circle_center, points):
    angles = []
    for point in points:
        delta_x = point[0] - circle_center[0]
        delta_y = point[1] - circle_center[1]
        angle = np.arctan2(delta_y, delta_x)
        angles.append(angle)
    
    return np.array(angles)

def circle_fit(data):
    xc,yc,r, sigma= cf.standardLSQ(data)
    angles = angle_on_circle([xc, yc], data)
    print(f'fitted circle: x {xc:.2f} y {yc:.2f} r {r:.2f} sigma {sigma:.2f}')   
    return xc, yc, r, sigma, angles

def calc_circle_fit_err(data, xc, yc, r):
    points = np.asarray(data)
    
    # Calculate the Euclidean distance from each point to the circle's center
    distances_to_center = np.sqrt((points[:, 0] - xc)**2 + (points[:, 1] - yc)**2)
    
    # Calculate the projected distance to the circle's edge
    projected_distances = distances_to_center - r
    
    return projected_distances

def calc_angle_var(data):
    def get_stat(x):
        if len(x.shape) == 1:
            print('range', np.max(x) - np.min(x), 'std', np.std(x))
        elif len(x.shape) == 2:
            range_arr = np.max(x, axis=0) - np.min(x ,axis=0)
            std_arr = np.std(x, axis=0)
            # print('range', range_arr[0], x.shape)
            print('max range', np.max(range_arr), 'avg range', np.mean(range_arr), 'range std', np.std(range_arr))
    start_point_arr = np.array([np.min(x) for x in data] + [x[0] for x in data])
    end_point_arr = np.array([np.max(x) for x in data])
    mid_point_arr = np.array([x[len(data[0])//2-2] for x in data])
    min_length = min(len(arr) for arr in data)
    
    # Trim each array to the shortest length
    data_trimmed = np.vstack([arr[:min_length] for arr in data])
    
    print('start')
    get_stat(start_point_arr)
    print('mid')
    get_stat(mid_point_arr)
    print(mid_point_arr)
    print('end')
    get_stat(end_point_arr)
    print('traj')
    get_stat(data_trimmed)

def rc_charging(t, A, tau, C):
    return A * (1 - np.exp(-t / tau)) + C

def rc_charging_linear(t, ln_A, inv_tau, ln_C):
    return ln_A - (1 / inv_tau) * t + ln_C

# Define the Huber loss function
def huber_loss(residuals, delta):
    return np.where(np.abs(residuals) <= delta, 0.5 * residuals ** 2, delta * (np.abs(residuals) - 0.5 * delta))

# Define the objective function using Huber loss
def objective(params, t, y, delta):
    A, tau, C = params
    residuals = y - rc_charging(t, A, tau, C)
    return np.sum(huber_loss(residuals, delta))

def exponential_fit(x, y):
    """
    Fit a shifted exponential curve to the given (x, y) data points using linear least squares regression.

    Parameters:
        x (array-like): Independent variable data points.
        y (array-like): Dependent variable data points.

    Returns:
        tuple: Fitted parameters (a, b, c) of the shifted exponential function.
    """
    popt, pcov = curve_fit(rc_charging, x, y, p0=(2, 6, -10), method='trf', maxfev=20000)
    A_fit, tau_fit, C_fit = popt

    # result = minimize(objective, (2, 6, -10), args=(x,y, 1000), method='Nelder-Mead')
    # A_fit, tau_fit, C_fit = result.x
    # Extract fitted parameters
    # print(A_fit, tau_fit, C_fit)
    

    # ln_y_data = np.log(y)
    # popt, pcov = curve_fit(rc_charging_linear, x, ln_y_data)

    # # Extract fitted parameters
    # ln_A_fit, inv_tau_fit, ln_C_fit = popt
    # A_fit = np.exp(ln_A_fit)
    # tau_fit = 1 / inv_tau_fit
    # C_fit = np.exp(ln_C_fit)
    # print('hello')
    return A_fit, tau_fit, C_fit


def make_custom_formatter(old_min, old_max, new_min, new_max, display_int=False):
    def custom_formatter(x, pos):
        old_range = old_max - old_min
        new_range = new_max - new_min
        new_value = (((x - old_min) * new_range) / old_range) + new_min
        if not display_int:
            return f'{new_value:.1f}'
        else:
            return int(round(new_value))

    return custom_formatter

def get_change_idx(arr):
    # given an array, return indices right before the value changes from one to the next
    # if arr=[1, 1, 2, 2, 3, 3, 3], it will return [1, 3, 6]
    indices = np.where(arr[:-1] != arr[1:])[0] 
    indices = np.append(indices, arr.shape[0]-1)
    # print(indices)
    return indices

def filter_PWM(value_arr, time_arr):
    idx = get_change_idx(value_arr)
    return value_arr[idx], time_arr[idx]

def load_data(result_folder, use_buffer=False):
    video_dir = os.path.join(result_folder, 'output_video_0.avi')
    pickle_dir = os.path.join(result_folder, 'queue.pickle')
    processed_folder = os.path.join(result_folder, 'processed')
    processed_data_dir = os.path.join(processed_folder, 'load_data.pkl')

    if use_buffer:
        # print(processed_data_dir, os.path.exists(processed_data_dir))
        if os.path.exists(processed_data_dir):
            print('found prior buffer, loading from {}'.format(processed_data_dir))
            with open(processed_data_dir, "rb") as f:
                processed_data = pickle.load(f)
            print(processed_data.keys())
            return processed_data['values_dict'], processed_data['times_dict'], processed_data['camera_dict']

    with open(pickle_dir, "rb") as f:
        data = pickle.load(f)
        print(len(data))
    
    aruco_detector = None

    values_dict = {}
    times_dict = {}
    camera_dict = {'corners':[], 'ids': [], 'corners_by_id': []}

    def add_pair(device, device_id, value, time):
        # Store the values and times in the respective dictionaries
        key = f"{device}_{device_id}"
        if key not in values_dict:
            values_dict[key] = []
            times_dict[key] = []
        values_dict[key].append(value)
        times_dict[key].append(time)

    # Iterate through the data
    for j, entry in enumerate(data):
        device, device_id, value, time = entry
        if j % 1000 == 0:
            print(j, len(data))
        # If the device is a camera, compute data
        if device == "camera":
            if aruco_detector is None:
                aruco_detector = utils_aruco.ArucoDetector(video_dir)
            corners, ids = aruco_detector.detect_frame()
            
            # assert not (corners is None)
            if corners is None:
                print('corners none', j)
                continue
            if len(corners) == 0:
                continue
            corners = np.array(corners)[:, 0, :]
            ids = np.array(ids)[:, 0]
            camera_dict['corners'].append(corners)
            camera_dict['ids'].append(ids)

            corners_by_id = {}
            for j, tmp_id in enumerate(ids):
                corners_by_id[tmp_id] = corners[j]
            camera_dict['corners_by_id'].append(corners_by_id)

            for i, id in enumerate(ids):
                add_pair(device, id, corners[i], time)
                
        add_pair(device, device_id, value, time)
    if not (aruco_detector is None):
        assert aruco_detector.detect_frame()[0] is None
    # Convert lists to numpy arrays for easier processing
    for key in values_dict:
        values_dict[key] = np.array(values_dict[key])
        times_dict[key] = np.array(times_dict[key])

    if not os.path.exists(processed_folder):
        os.mkdir(processed_folder)

    if use_buffer:
        # Open the file in binary write mode
        with open(processed_data_dir, 'wb') as f:
            # Serialize and write the dictionary to the file
            pickle.dump({
                'values_dict': values_dict,
                'times_dict': times_dict,
                'camera_dict': camera_dict
            }, f)
        print('saving to {}'.format(processed_data_dir))

    return values_dict, times_dict, camera_dict

def cmp_avg_side_len(c):
    side_lengths = [np.linalg.norm(c[i] - c[(i + 1) % 4]) for i in range(4)]
    average_length = np.mean(side_lengths)
    return average_length

def cmp_corners(c1, c2):
    c1_centroid = np.mean(c1, 0)
    c2_centroid = np.mean(c2, 0)
    disp = c2_centroid - c1_centroid
    pose_angle, pose_xy = utils_icp.best_fit_transform_2d(c1, c2-disp)
    #compute z displacement
    c1_len = cmp_avg_side_len(c1)
    c2_len = cmp_avg_side_len(c2)
    pose_z = c2_len - c1_len
    pose_xyz = np.append(pose_xy, pose_z)
    # print('xyz', pose_xyz, 'angle', pose_angle)
    return {
        'disp': disp,
        'rot': pose_angle
    }

def rotate_point(xy, offset, rot):
    """
    Rotate a point (a_offset, b_offset) around the center (x, y) by angle rot.
    """
    # Convert rot to radians
    x = xy[0]
    y = xy[1]
    a_offset = offset[0]
    b_offset = offset[1]
    theta = np.radians(rot)
    
    # Define rotation matrix
    rotation_matrix = np.array([[np.cos(theta), -np.sin(theta)],
                                 [np.sin(theta), np.cos(theta)]])
    
    # Offset the point from the center
    offset_point = np.array([a_offset, b_offset])
    
    # Rotate the offset point
    rotated_offset_point = np.dot(rotation_matrix, offset_point)
    
    return rotated_offset_point

def calc_angle_wrt_horiz(corners):
    return cmp_corners(corners, np.array(
                [
                     [0, 0], [1, 0], [1, 1],[0, 1], 
                ]
            ))['rot']
def multi_data_prep(data_folder, result_name_arr, ids, tag_offset_dict={}, no_camera=False):
    values_dict_arr = []
    times_dict_arr = []
    camera_dict_arr = []
    camera_data_dict_arr = []
    camera_scale_arr = []

    for result_name in result_name_arr:
        result_folder = os.path.join(data_folder, result_name)
        if len(result_name) > 0:
            values_dict, times_dict, camera_dict, camera_data_dict, camera_scale = default_data_prep(result_folder, ids=ids, tag_offset_dict=tag_offset_dict, no_camera=no_camera)
        else:
            values_dict = {}
            times_dict = {}
            camera_dict = {}
            camera_data_dict = {}
            camera_scale = []
        values_dict_arr.append(values_dict)
        times_dict_arr.append(times_dict)
        camera_dict_arr.append(camera_dict)
        camera_data_dict_arr.append(camera_data_dict)
        camera_scale_arr.append(camera_scale)

    return values_dict_arr, times_dict_arr, camera_dict_arr, camera_data_dict_arr, camera_scale_arr


def default_data_prep(result_folder, ids=[], tag_offset_dict={}, no_camera=False):
    values_dict, times_dict, camera_dict = load_data(result_folder, use_buffer=True)
    video_path = os.path.join(result_folder, 'output_video_0.avi')

    if not ('camera_time' in times_dict) or no_camera:
        return values_dict, times_dict, {}, {}, None
    
    for nth_frame in [1, 1000]:
        output_path = os.path.join(result_folder, 'processed', f'frame_{nth_frame}.png')
        save_nth_frame(video_path, nth_frame, output_path, use_buffer=True)

    print('loaded keys: ', list(times_dict.keys()))
    camera_time = times_dict['camera_time']
    camera_data_dict_by_id = {}
    first_base_corners = None
    last_base_corners = None

    if not (0 in ids):
        first_base_corners = np.array([
            [0,1],
            [1,1],
            [1,0],
            [0,0]
        ])*1. 
        last_base_corners = np.copy(first_base_corners)

    first_camera_scale = 0
    for i, t in enumerate(camera_time):
        cur_ids = camera_dict['ids'][i]
        cur_corners = camera_dict['corners'][i]
        corners_by_id = camera_dict['corners_by_id'][i]


        if 0 in corners_by_id:
            if first_base_corners is None:
                first_base_corners = corners_by_id[0]
                first_camera_scale = 3 / cmp_avg_side_len(first_base_corners)
            last_base_corners = corners_by_id[0]
        
        for id in corners_by_id:
            if len(ids)!=0 and not(id in ids):
                continue
            if not (id in camera_data_dict_by_id):
                camera_data_dict_by_id[id] = []
            if last_base_corners is None:
                continue
            
            angle_wrt_horiz = calc_angle_wrt_horiz(corners_by_id[id])
            data_dict = cmp_corners(last_base_corners, corners_by_id[id])
            data_dict.update({
                't': t,
                'corners_len': cmp_avg_side_len(corners_by_id[id]),
                'angle_wrt_fixed': cmp_corners(first_base_corners, corners_by_id[id])['rot'],
                'angle_wrt_horiz': angle_wrt_horiz,
                'centroid': np.mean(corners_by_id[id], 0),
                'corners': corners_by_id[id]
            })
            
            if id in tag_offset_dict:
                offset = rotate_point(data_dict['centroid'], np.array(tag_offset_dict[id]['disp']) / first_camera_scale,  tag_offset_dict[id]['rot']-angle_wrt_horiz)
            else:
                offset = np.array([0, 0])
            corners_offset = corners_by_id[id] + offset
            transform_offset = cmp_corners(last_base_corners, corners_offset)
            data_dict.update({
                'disp_offset': transform_offset['disp'],
                'rot_offset': transform_offset['rot'],
                'centroid_offset': np.mean(corners_offset, 0),
                'corners_offset': corners_offset
            })

            camera_data_dict_by_id[id].append(
                copy.deepcopy(data_dict)
            )

    camera_data_dict = {}
    for id in camera_data_dict_by_id:
        for data_dict in camera_data_dict_by_id[id]:
            for key,item in data_dict.items():
                if not (key in camera_data_dict):
                    camera_data_dict[key] = {}
                if not id in camera_data_dict[key]:
                    camera_data_dict[key][id] = []
                camera_data_dict[key][id].append(item)    
    for key in camera_data_dict:
        for id in camera_data_dict[key]:
            camera_data_dict[key][id] = np.array(camera_data_dict[key][id])
    print(camera_data_dict.keys())
    if 1 in camera_data_dict['corners_len']:
        camera_scale = 3/camera_data_dict['corners_len'][1]
    else:
        camera_scale = 3/camera_data_dict['corners_len'][2]

    return values_dict, times_dict, camera_dict, camera_data_dict, camera_scale

def calc_cycle(n_cycle, x_time, r_time):
    # assert x_time is monotonically increasing
    x_time = np.copy(x_time)
    assert np.all(np.diff(x_time) >= 0)

    # given regulator time sequence, divide x_time into each cycle
    cycle_len = r_time.shape[0]//n_cycle
    t_cycle = []
    cycle_start_idx = []
    for i in range(n_cycle):
        start_idx = i * cycle_len
        end_idx = (i+1)*cycle_len
        middle_idx = start_idx + (end_idx-start_idx)//2
        start_t = r_time[start_idx]
        middle_t = r_time[middle_idx]  
        end_t = r_time[end_idx] if i < n_cycle-1 else r_time[-1]

        min_idx = np.where(x_time >= start_t)[0][0]
        middle_idx = np.where(x_time < middle_t)[0][-1] + 1
        max_idx = np.where(x_time <= end_t)[0][-1] + 1
        x_time[min_idx:middle_idx] -= start_t
        x_time[middle_idx:max_idx] = end_t - x_time[middle_idx:max_idx]
        if i == 0:
            cycle_start_idx = []
            t_cycle = np.copy(x_time)
        t_cycle[min_idx:max_idx] = x_time[min_idx:max_idx]
        cycle_start_idx.append(min_idx)
        if i == n_cycle-1:
            cycle_start_idx.append(max_idx)

    return t_cycle, cycle_start_idx

def prep_cycle(n_cycle, camera_data_dict, r_val, r_time):
    # assert r_val.shape[0]%n_cycle == 0
    # for i in range(n_cycle):
        # assert np.array_equal(r_val[:cycle_len], r_val[i*cycle_len:(i+1)*cycle_len])
    cycle_len = r_val.shape[0]//n_cycle
    print('n cycle', n_cycle, 'cycle len', cycle_len)
    camera_data_dict['t_cycle'] = {}
    camera_data_dict['cycle_start_idx'] = {}
    
    for id in camera_data_dict['t']:
        t_cycle, cycle_start_idx = calc_cycle(n_cycle, camera_data_dict['t'][id], r_time)
        camera_data_dict['t_cycle'][id] = np.copy(t_cycle)
        camera_data_dict['cycle_start_idx'][id] = np.copy(cycle_start_idx)
    for id in camera_data_dict['cycle_start_idx']:
        assert len(camera_data_dict['cycle_start_idx'][id]) == n_cycle+1
    return cycle_len

def merge_PWM(v1, v2):
    v = np.copy(v1)
    v[v2!=0] = -v2[v2!=0]
    return v
if __name__ == '__main__':
    data_folder = './data-raw'
    result_name = 'exp_2_1_20240221_19-43-22'
    result_name_arr = [folder for folder in os.listdir(data_folder) if os.path.isdir(os.path.join(data_folder, folder))]
    for result_name in result_name_arr:
        if result_name == 'debug':
            continue
        print('loading {}'.format(result_name))
        try:
            values_dict, times_dict, camera_dict = load_data(os.path.join(data_folder, result_name), use_buffer=True)
        except:
            print('error')
            continue
    # print('loaded keys: ', list(times_dict.keys()))

    # print(times_dict['camera_time'].shape)
    # camera_time = times_dict['camera_time']
    # camera_data_dict_by_id = {
    # }
    # last_base_corners = None
    # for i, t in enumerate(camera_time):
    #     cur_ids = camera_dict['ids'][i]
    #     cur_corners = camera_dict['corners'][i]
    #     corners_by_id = camera_dict['corners_by_id'][i]

    #     if 0 in corners_by_id:
    #         last_base_corners = corners_by_id[0]
        
    #     for id in corners_by_id:
    #         if not (id in camera_data_dict_by_id):
    #             camera_data_dict_by_id[id] = []
    #         data_dict = cmp_corners(last_base_corners, corners_by_id[id])
    #         data_dict.update({
    #             't': t,
    #             'corners_len': cmp_avg_side_len(corners_by_id[id])
    #         })
    #         camera_data_dict_by_id[id].append(
    #             copy.deepcopy(data_dict)
    #         )

    # camera_data_dict = {}
    # for id in camera_data_dict_by_id:
    #     for data_dict in camera_data_dict_by_id[id]:
    #         for key,item in data_dict.items():
    #             if not (key in camera_data_dict):
    #                 camera_data_dict[key] = {}
    #             if not id in camera_data_dict[key]:
    #                 camera_data_dict[key][id] = []
    #             camera_data_dict[key][id].append(item)    
    # for key in camera_data_dict:
    #     for id in camera_data_dict[key]:
    #         camera_data_dict[key][id] = np.array(camera_data_dict[key][id])

    # simplePlot(camera_data_dict['t'][1], camera_data_dict['rot'][1])
    # simplePlot(camera_data_dict['t'][1], camera_data_dict['disp'][1][:, 2]/camera_data_dict['corners_len'][1]*3)
    # simpleGradient(camera_data_dict['disp'][1][:, 0]/camera_data_dict['corners_len'][1]*3, camera_data_dict['disp'][1][:, 1]/camera_data_dict['corners_len'][1]*3, camera_data_dict['t'][1], invert_x=True)

    # # print(camera_data_dict['disp'][1])

    
        


    # # Accessing data for specific devices
    # sensor_1_values = values_dict["sensor_sensor1"]
    # sensor_1_times = times_dict["sensor_sensor1"]

    # regulator_PWM1_values = values_dict["regulator_PWM1"]
    # regulator_PWM1_times = times_dict["regulator_PWM1"]

    # camera_1_centroids = values_dict["camera_4"]
    # camera_1_time = times_dict["camera_4"]

    # camera_base_centroids = values_dict["camera_0"]
    # camera_base_time = times_dict["camera_0"]

    # simplePlot(sensor_1_times, sensor_1_values)
    # simplePlot(regulator_PWM1_times, regulator_PWM1_values)

    # #Perform linear interpolation of data to get value at constant time interval
    # sensor_1_values_itp, sensor_1_times_itp     = interpolate_sensors(sensor_1_values, sensor_1_times)
    # regulator_PWM1_values_itp, regulator_PWM1_times_itp = interpolate_sensors(regulator_PWM1_values, regulator_PWM1_times)
    # camera_1_centroids_itp, camera_1_time_itp           = interpolate_centroids(camera_1_centroids, camera_1_time)
    # camera_base_centroids_itp, camera_base_time_itp     = interpolate_centroids(camera_base_centroids, camera_base_time)

    # #Trim everything to have the same effective time range
    # (
    #     sensor1_value_trimed, sensor1_time_trimed,
    #     regulator1_value_trimed, regulator1_time_trimed,
    #     camera_1_centroids_trimed, camera_1_time_trimed,
    #     camera_base_centroids_trimed, camera_base_time_trimed
    # ) = trim_effective(
    #     sensor_1_values_itp, sensor_1_times_itp,
    #     regulator_PWM1_values_itp, regulator_PWM1_times_itp,
    #     camera_1_centroids_itp, camera_1_time_itp,
    #     camera_base_centroids_itp, camera_base_time_itp
    # )

    # # plot_sensors(regulator1_value_trimed, regulator1_time_trimed, sensor1_value_trimed, sensor1_time_trimed, 'Input Pressure(PSI)', 'Output Pressure(PSI)')

    # # displacement, displacement_time = calculate_displacement(camera_base_centroids_trimed, camera_base_time_trimed, camera_1_centroids_trimed, camera_1_time_trimed)

    # simplePlot(camera_1_time_trimed, camera_1_centroids_trimed[:, 0])

    # # simpleGradient(displacement,sensor1_value_trimed,displacement_time,'displacement(mm)','pressure(PSI)','time')

    # # simpleGradient(regulator1_value_trimed, sensor1_value_trimed, sensor1_time_trimed, 'time', 'Error(Input-Output)', 'output pressure(PSI)')

    # # plot_centroids_with_gradient(camera_1_centroids_trimed, camera_1_time_trimed)

    # # simplePlot2(sensor1_time_trimed,regulator1_value_trimed,sensor1_value_trimed,'time','pressure')
    # # simplePlot(sensor