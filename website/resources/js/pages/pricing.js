$(document).ready(function() {

  var usersLabel = $('#usersLabel span');
  var capacityLabel = $('#capacityLabel span');

  // Price Update
  function updatePrice() {
    var current_users = parseInt($(usersLabel).text());
    var current_storage = parseInt($(capacityLabel).text());
    var price_per_gb = $('#storageSelect option:selected').attr('data-price');
    var price_per_gb_aerofs = 0.13;
    var max_users = $('.plan.startup p.max_users').attr('data-max-users');
    var price_per_user = $('.plan.business p.price').attr('data-price');
    var price_per_user_dropbox = 12;
    var price_per_user_aerofs = 15;
    var price = 0;
    var price_dropbox = 0;
    var price_aerofs = 0;
    var porcentage;

    // yearly price
    if (current_users >= max_users) {
      price =  price_per_user * 12 * current_users;
    }

    // yearly price with storage
    price += (current_storage * 1000 * price_per_gb);

    // display aerofs comparison when > 30 users
    // else display dropbox
    if (current_users < 100 && (current_storage === 1 || current_storage === 0)) {
      $('.price .service').text('Dropbox');
      price_dropbox = price_per_user_dropbox * 12 * current_users;
      porcentage = 100 - (price / price_dropbox * 100);
    } else {
      $('.price .service').text('AeroFS');
      price_aerofs = price_per_user_aerofs * 12 * current_users;
      price_aerofs += current_storage * 1000 * price_per_gb_aerofs;
      porcentage = 100 - (price / price_aerofs * 100);
    }

    $('.price .porcentage').text(Math.round(porcentage));

    if (price === 0) {
      $('.estimation p.free').show();
      $('.estimation p.infinit').hide();
    } else {
      $('.estimation p.infinit span').text(price);
      $('.estimation p.infinit').show();
      $('.estimation p.free').hide();
    }

  }

  // Users Slider
  $("#usersInput").bind("slider:changed", function (event, data) {
    usersLabel.text(data.value);
    updatePrice();
  });

  // Capacity Slider
  $("#capacityInput").bind("slider:changed", function (event, data) {
    capacityLabel.text(data.value);
    updatePrice();
  });

  $('#storageSelect').on('change', function() {
    updatePrice();
  });

  updatePrice();

});