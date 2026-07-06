import os
import sys
import yaml

sys.path.insert(0, os.path.dirname(__file__))
from web_editor_resolve import readme_editor_url, github_pages_base, normalize_card_info

# Define the base folder and README file path
base_folder = 'releases'
readme_path = os.path.join(base_folder, 'README.md')
repo_slug = os.environ.get('GITHUB_REPOSITORY', 'TomWhitwell/Workshop_Computer')
pages_base = github_pages_base(repo_slug)


# Function to read data from a YAML file
def read_data(file_path):
    with open(file_path, 'r') as file:
        data = yaml.safe_load(file)
    return data if isinstance(data, dict) else {}


def read_card_info(folder_path):
    info_path = os.path.join(folder_path, 'info.yaml')
    if os.path.isfile(info_path):
        return read_data(info_path)
    return {}

# Function to update the README file
def update_readme(folders_data):
    # Define the desired order of columns
    desired_order = ['Description', 'Version', 'Language', 'Creator']

    # Standard table columns
    ordered_keys = desired_order

    # Create new table headers and dividers
    headers = ['Folder Name'] + ordered_keys
    header_line = '| ' + ' | '.join(headers) + ' |\n'
    divider_line = '| ' + ' | '.join(['-' * len(header) for header in headers]) + ' |\n'

    new_table = [header_line, divider_line]

    # Create table rows
    sorted_folders = sorted(folders_data.keys())
    for folder in sorted_folders:
        raw = folders_data[folder]
        d = normalize_card_info(raw)
        row = [folder]
        descr = str(d.get('description') or d.get('summary') or '')
        editor_url = readme_editor_url(folder, os.path.join(base_folder, folder), raw, pages_base)
        if editor_url:
            descr += '<br>[Web editor](' + editor_url + ')'
        row += [descr]
        vers = '' if d.get('version') is None else str(d.get('version'))
        status = d.get('status')
        if status not in (None, ''):
            vers += '<br>' + str(status)
        row += [vers]
        row += [str(d.get('language') or '')]
        row += [str(d.get('creator') or '')]
        new_table.append('| ' + ' | '.join(row) + ' |\n')

    # Write the new content to the README.md file
    with open(readme_path, 'w') as readme_file:
        readme_file.write('# Releases  \n')
        readme_file.writelines(new_table)

def main():
    folders_data = {}
    for folder in os.listdir(base_folder):
        folder_path = os.path.join(base_folder, folder)
        if os.path.isdir(folder_path):
            folders_data[folder] = read_card_info(folder_path)

    update_readme(folders_data)

if __name__ == "__main__":
    main()
